(ns webserv-tests.upload-test
  "Non-CGI POST (file upload) regression tests.

  The subject requires that 'clients must be able to upload files'. handle_setup()
  calls setup_post(), which opens <upload_directory><path> in append mode,
  handle_post_leftover() flushes the body bytes already sitting in the scratch
  buffer, and handle_post() drains the rest of the socket into the file until
  content_length bytes have been written.

  Both sizes are covered on purpose. The small body fits in a single
  client_header_buffer_size buffer and is fully written by handle_post_leftover
  alone; the large one cannot, so it only lands intact if handle_post() keeps
  feeding _stream across several reads. That second path was broken once — it
  jumped straight to the response and a 5 KB upload jumped to ~1 KB with the
  client still getting a 201 — so the large-body case stays as a regression
  guard.

  Note the append mode: a repeated POST to the same URI grows the file rather
  than replacing it, which is why every test starts from a guaranteed-absent
  probe via with-clean-probe."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server])
  (:import [java.io File]))

(use-fixtures :each (server/make-fixture "upload.conf"))

;; Several times the 1024-byte default buffer, so the body cannot arrive in one
;; read and the truncation is unambiguous.
(def ^:private big-size 5000)

;; upload.conf sets `upload_directory __WWWROOT__/upload upload`, so a POST to
;; /<name> is written to www/upload/<name> and served back from /upload/<name>.
(defn- probe-file [name] (File. (str "../www/upload/" name)))

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
  (testing "a POST body smaller than the request buffer is written to disk in full"
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
  (testing "a POST body larger than one buffer is written to disk in full"
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
          (is (= big-size (:content-length (server/http-get-bytes (str "/upload/" name))))
              "the uploaded file must serve back at its full length"))))))
