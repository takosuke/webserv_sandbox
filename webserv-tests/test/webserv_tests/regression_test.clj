(ns webserv-tests.regression-test
  "Regression tests for known, documented issues (see Architecture.md
  \"Subject scope & known gaps\").

  Each test asserts the *desired* behavior. Tests marked KNOWN-FAILING encode a
  bug that is not fixed yet — they are expected to fail today and should turn
  green when the corresponding bug is fixed, at which point the marker can be
  removed. Tests here are chosen so they do not crash the server, so a single
  :once fixture is safe. Parser errors that crash the server live in
  error_response_test.clj / parse_error_test.clj (which use :each)."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "base.conf"))

;; ---------------------------------------------------------------------------
;; Binary file integrity (NUL-truncation bug in ScratchBuffer)
;; ---------------------------------------------------------------------------
;; www/static/image.png is a PNG whose first NUL byte is at offset 8. The
;; buffer is C-string oriented, so the body is truncated at that NUL: the
;; response advertises the real Content-Length (from stat) but only sends a
;; handful of bytes.

(def ^:private png-path "../www/static/image.png")

(deftest test-binary-file-served-intact
  (testing "KNOWN-FAILING: GET on a binary file returns the whole file, not a NUL-truncated prefix"
    (let [on-disk   (.length (java.io.File. png-path))
          {:keys [status body-bytes]} (server/http-get-bytes "/static/image.png")]
      (is (= 200 status))
      (is (= on-disk (alength body-bytes))
          (str "served " (alength body-bytes) " bytes of a " on-disk "-byte file")))))

(deftest test-binary-body-matches-content-length
  (testing "KNOWN-FAILING: bytes delivered equal the advertised Content-Length"
    (let [{:keys [content-length body-bytes]} (server/http-get-bytes "/static/image.png")]
      (is (some? content-length))
      (is (= content-length (alength body-bytes))
          (str "Content-Length header says " content-length
               " but " (alength body-bytes) " bytes were delivered")))))

;; ---------------------------------------------------------------------------
;; DELETE actually deletes (no unlink/remove in the handler)
;; ---------------------------------------------------------------------------

(deftest test-delete-removes-file
  (testing "KNOWN-FAILING: DELETE removes the target file from disk"
    (let [f (java.io.File. "../www/deleteme.txt")]
      (spit f "temporary content")
      (try
        (let [resp (server/http-request
                     "DELETE /deleteme.txt HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")]
          (is (contains? #{200 202 204} (:status resp))
              "DELETE should report success")
          (is (not (.exists f))
              "file should no longer exist after a successful DELETE"))
        (finally
          (when (.exists f) (.delete f)))))))

;; ---------------------------------------------------------------------------
;; Header line without a colon must be rejected (the `if (!colon)` bug)
;; ---------------------------------------------------------------------------

(deftest test-header-without-colon-rejected
  (testing "KNOWN-FAILING: a header line with no colon yields 400, not 200"
    (let [resp (server/http-request
                 (str "GET /index.html HTTP/1.1\r\n"
                      "Host: 127.0.0.1\r\n"
                      "BadHeaderNoColon\r\n"
                      "Connection: close\r\n\r\n"))]
      (is (= 400 (:status resp))))))

;; ---------------------------------------------------------------------------
;; CGI CONTENT_LENGTH / CONTENT_TYPE forwarding (env vars never set)
;; ---------------------------------------------------------------------------
;; env.py echoes CONTENT_LENGTH, CONTENT_TYPE and the body it read from stdin.

(defn- cgi-env-post [body content-type]
  (server/http-request
    (str "POST /cgi-bin/env.py HTTP/1.1\r\n"
         "Host: 127.0.0.1\r\n"
         "Content-Type: " content-type "\r\n"
         "Content-Length: " (count body) "\r\n"
         "\r\n" body)))

(deftest test-cgi-content-length-forwarded
  (testing "KNOWN-FAILING: CONTENT_LENGTH env var is set for a CGI POST"
    (let [resp (cgi-env-post "hello" "text/plain")]
      (is (clojure.string/includes? (:body resp) "CONTENT_LENGTH=5")))))

(deftest test-cgi-content-type-forwarded
  (testing "KNOWN-FAILING: CONTENT_TYPE env var is forwarded to the CGI"
    (let [resp (cgi-env-post "hello" "application/x-test")]
      (is (clojure.string/includes? (:body resp) "CONTENT_TYPE=application/x-test")))))

(deftest test-cgi-post-body-readable-via-content-length
  (testing "KNOWN-FAILING: a CGI reading CONTENT_LENGTH bytes from stdin sees the body"
    (let [resp (cgi-env-post "hello" "text/plain")]
      (is (clojure.string/includes? (:body resp) "BODY=hello")))))

;; ---------------------------------------------------------------------------
;; Chunked transfer-encoding is un-chunked before reaching the CGI
;; ---------------------------------------------------------------------------

(deftest test-chunked-post-unchunked-for-cgi
  (testing "KNOWN-FAILING: a chunked POST body is de-chunked and delivered to the CGI"
    (let [resp (server/http-request
                 (str "POST /cgi-bin/env.py HTTP/1.1\r\n"
                      "Host: 127.0.0.1\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "\r\n"
                      "5\r\nhello\r\n"
                      "6\r\n world\r\n"
                      "0\r\n\r\n"))]
      ;; The CGI should see the reassembled body "hello world", never the raw
      ;; chunk framing.
      (is (some? (:body resp)))
      (is (clojure.string/includes? (:body resp) "BODY=hello world"))
      (is (not (clojure.string/includes? (:body resp) "5\r\nhello"))))))
