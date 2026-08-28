(ns webserv-tests.cgi-header-buffer-test
  "The ceiling on a CGI response header is client_header_buffer_size, not a
  hard-coded cap.

  ClientConnection reuses one ScratchBuffer per connection for both request
  parsing and response-header staging, and buffer_res_headers can place any
  header that fits in it. Under base.conf's 1024-byte default, big_header.py's
  ~3 KB X-Big header does not fit and the request is answered 502 — pinned by
  cgi-robustness-test/test-cgi-header-over-buffer-is-502-not-a-hang. This
  namespace pins the other side of the same limit: configure the buffer large
  enough and the identical header is forwarded intact."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "cgi_big_header.conf"))

(deftest test-large-cgi-header-forwarded-when-buffer-fits
  (testing "a ~3 KB CGI header is forwarded intact under client_header_buffer_size 8192"
    (let [{:keys [response timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            "GET /cgi-bin/big_header.py HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" 2000)]
      (is (not timed-out) "the response must complete, not wedge")
      (is (re-find #"HTTP/\S+ 200" response)
          "a 200 status line should be present")
      (is (clojure.string/includes? response "BIGHEADERVALUE")
          "the large X-Big header should reach the client"))))
