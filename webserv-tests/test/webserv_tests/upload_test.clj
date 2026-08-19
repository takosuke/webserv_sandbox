(ns webserv-tests.upload-test
  "Non-CGI POST (file upload) regression tests.

  The subject requires that 'clients must be able to upload files'. The server
  implements this: handle_setup() calls setup_post(), which opens
  <root><path> in append mode, handle_post_leftover() flushes the body bytes
  that already sat in the scratch buffer, and handle_post() is supposed to keep
  draining the socket into the file until content_length bytes have been
  written.

  It does not. The _state == REQ_BODY branch of handle_post() reads from the
  socket and then does

      if (_buf.feed_capacity() > 0) { _state = RESPONSE; setup_res(); }

  i.e. it jumps to the response instead of feeding the buffer to the file. The
  half of handle_post() that actually writes to _stream is only reachable from
  CGI_TRANSMIT_BODY, which a static POST never enters. Net effect: only the
  bytes that happened to be in the buffer when setup finished ever reach disk —
  a 5 KB upload lands as ~1 KB and the client still gets 201.

  The small-body test is the control: it passes today and isolates the defect to
  bodies that do not fit in a single client_header_buffer_size buffer."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server])
  (:import [java.io File]))

(use-fixtures :each (server/make-fixture "upload.conf"))

;; Several times the 1024-byte default buffer, so the body cannot arrive in one
;; read and the truncation is unambiguous.
(def ^:private big-size 5000)

(defn- probe-file [name] (File. (str "../www/" name)))

(defn- post-file
  "POST body to /<name> and return the parsed response. The upload path opens
  the target in append mode, so callers must start from a non-existent file."
  [name body]
  (let [{:keys [response]}
        (server/raw-request-timeout "127.0.0.1" 8080
          (str "POST /" name " HTTP/1.1\r\n"
               "Host: 127.0.0.1\r\n"
               "Content-Length: " (count body) "\r\n\r\n"
               body)
          5000)]
    response))

(defn- with-clean-probe
  "Run f with a guaranteed-absent www/<name>, removing it afterwards."
  [name f]
  (let [f' (probe-file name)]
    (when (.exists f') (.delete f'))
    (try (f name)
         (finally (when (.exists f') (.delete f'))))))

(deftest test-small-upload-reaches-disk
  (testing "a POST body smaller than the request buffer is written to disk in full (control)"
    (with-clean-probe "upload_small_probe.txt"
      (fn [name]
        (let [body (apply str (repeat 64 "a"))
              resp (post-file name body)]
          (is (#{200 201 204} (server/status-code resp))
              "a static POST should be accepted")
          (is (.exists (probe-file name))
              "the upload should have created the target file")
          (is (= 64 (.length (probe-file name)))
              "every byte of a single-buffer body should reach disk"))))))

(deftest test-large-upload-is-not-truncated
  (testing "KNOWN-FAILING: a POST body larger than one buffer is written to disk in full"
    (with-clean-probe "upload_big_probe.txt"
      (fn [name]
        (let [body (apply str (repeat big-size "A"))
              resp (post-file name body)]
          (is (#{200 201 204} (server/status-code resp))
              "a static POST should be accepted")
          (is (= big-size (.length (probe-file name)))
              (str "the whole " big-size "-byte body must reach disk, not just the "
                   "bytes left in the scratch buffer after handle_setup"))
          ;; Reading it back over HTTP shows the same truncation from the
          ;; client's side, and fails even if the on-disk check is ever
          ;; satisfied by a partial flush that happens after the response.
          (is (= big-size (:content-length (server/http-get-bytes (str "/" name))))
              "the uploaded file must serve back at its full length"))))))
