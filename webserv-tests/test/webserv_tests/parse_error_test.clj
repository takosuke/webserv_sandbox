(ns webserv-tests.parse-error-test
  "Regression tests for the parse-error status-clobber bug.

  handle_req_line / handle_req_headers set a 4xx/5xx status on a malformed
  request, but handle_setup() then unconditionally resets _req.status to 200
  (or 201 for POST) and routes the request through normal Location resolution.
  As a result, malformed requests come back with a routing status (200/302/405)
  instead of the parse error the parser detected.

  Every test asserts the *desired* status (400) and is KNOWN-FAILING until the
  clobber is fixed. These requests exercise the parser before a Location is
  resolved, so — following error_response_test's convention — each test gets its
  own server via :each so a crash can never contaminate another test."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :each (server/make-fixture "base.conf"))

(deftest test-bare-method-line-is-400
  (testing "KNOWN-FAILING: a request line with only a method (no URI) returns 400"
    ;; Currently returns 405: the 400 set by the parser is overwritten and the
    ;; request is routed with an empty path.
    (is (= 400 (:status (server/http-request
                          "GET\r\nHost: 127.0.0.1\r\n\r\n"))))))

(deftest test-control-char-in-uri-is-400
  (testing "KNOWN-FAILING: a control character in the URI returns 400"
    ;; Currently returns 302: the parser flags it, setup clobbers to 200,
    ;; the missing file becomes 404, and the 404 error_page redirects (302).
    (is (= 400 (:status (server/http-request
                          (str "GET /a" (char 1) "b HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")))))))

(deftest test-invalid-percent-encoding-is-400
  (testing "KNOWN-FAILING: an invalid %-escape in the URI returns 400"
    (is (= 400 (:status (server/http-request
                          "GET /%zz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"))))))

(deftest test-content-length-trailing-junk-is-400
  (testing "KNOWN-FAILING: a Content-Length with trailing junk returns 400"
    ;; `iss >> content_length` on "10abc" extracts 10 and does not set failbit,
    ;; so the malformed length is accepted and the request is served 200.
    (is (= 400 (:status (server/http-request
                          (str "GET /index.html HTTP/1.1\r\n"
                               "Host: 127.0.0.1\r\n"
                               "Content-Length: 10abc\r\n"
                               "Connection: close\r\n\r\n")))))))

(deftest test-bogus-minor-version-is-400
  (testing "KNOWN-FAILING: a bogus HTTP minor version (HTTP/1.10) returns 400"
    ;; The version check only requires the first 8 chars to be HTTP/1.1 or
    ;; HTTP/1.0 followed by zeros, so HTTP/1.10 is accepted as valid and served.
    (is (= 400 (:status (server/http-request
                          "GET /index.html HTTP/1.10\r\nHost: 127.0.0.1\r\n\r\n"))))))

(deftest test-server-survives-malformed-request-lines
  (testing "Server stays responsive after a batch of malformed request lines"
    ;; Not a clobber assertion — a resilience check: whatever the status codes,
    ;; the server must not crash and must still serve a normal request.
    (doseq [req ["GET\r\nHost: 127.0.0.1\r\n\r\n"
                 "GET /%zz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
                 (str "GET /a" (char 1) "b HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")]]
      (server/http-request req))
    (is (= 200 (:status (server/http-get "/index.html"))))))
