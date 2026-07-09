(ns webserv-tests.cgi-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "base.conf"))

;; ---- basic execution ----

(deftest test-cgi-executes-and-responds
  (testing "GET on a CGI script produces a response (not a connection close)"
    (let [resp (server/http-get "/cgi-bin/hello.py")]
      (is (some? (:status resp))))))

(deftest test-cgi-status-forwarded
  (testing "Status header set by the CGI script is forwarded to the client"
    (is (= 400 (:status (server/http-get "/cgi-bin/hello.py"))))))

(deftest test-cgi-body-content
  (testing "Body returned by the CGI script contains expected output"
    (let [resp (server/http-get "/cgi-bin/hello.py")]
      (is (clojure.string/includes? (:body resp) "Hello, World!")))))

(deftest test-cgi-content-type-forwarded
  (testing "Content-Type header set by the CGI script is forwarded"
    (let [resp (server/http-get "/cgi-bin/hello.py")]
      (is (= "text/plain" (get-in resp [:headers "content-type"]))))))

;; Regression: the CGI header parser once read every line after the first as
;; a prefix of the buffer's *first* line (missing line_start offset), so only
;; single-header CGI responses survived intact.  Pin a multi-header response
;; with exact values for the second and later headers.
(deftest test-cgi-multi-header-response-intact
  (testing "All headers of a multi-header CGI response reach the client with exact values"
    (let [resp (server/http-get "/cgi-bin/multi_header.py")]
      (is (= 200 (:status resp)))
      (is (= "text/plain" (get-in resp [:headers "content-type"])))
      (is (= "alpha-value-4242" (get-in resp [:headers "x-first"])))
      (is (= "beta" (get-in resp [:headers "x-second"])))
      (is (clojure.string/includes? (:body resp) "multi-header-marker")))))

;; ---- echo script: env var forwarding ----

(deftest test-cgi-request-method-env
  (testing "REQUEST_METHOD env var is set to GET for a GET request"
    (let [resp (server/http-get "/cgi-bin/echo.py")]
      (is (clojure.string/includes? (:body resp) "METHOD=GET")))))

(deftest test-cgi-query-string-forwarded
  (testing "QUERY_STRING env var contains the URL query string"
    (let [resp (server/http-get "/cgi-bin/echo.py?foo=bar&baz=1")]
      (is (clojure.string/includes? (:body resp) "QUERY=foo=bar&baz=1")))))

(deftest test-cgi-empty-query-string
  (testing "QUERY_STRING is empty when no query string is provided"
    (let [resp (server/http-get "/cgi-bin/echo.py")]
      (is (clojure.string/includes? (:body resp) "QUERY=\n")))))

;; ---- POST body forwarding ----

(deftest test-cgi-post-body-forwarded
  (testing "POST body is passed to the CGI script via stdin"
    (let [body "hello from post"
          resp (server/http-request
                 (str "POST /cgi-bin/echo.py HTTP/1.1\r\n"
                      "Host: 127.0.0.1\r\n"
                      "Content-Length: " (count body) "\r\n"
                      "\r\n"
                      body))]
      (is (clojure.string/includes? (:body resp) (str "BODY=" body))))))

(deftest test-cgi-post-method-env
  (testing "REQUEST_METHOD is POST for a POST request"
    (let [resp (server/http-request
                 "POST /cgi-bin/echo.py HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n")]
      (is (clojure.string/includes? (:body resp) "METHOD=POST")))))

;; ---- missing CGI script ----

(deftest test-missing-cgi-script-is-error
  (testing "Requesting a non-existent CGI script produces an error response"
    (let [resp (server/http-get "/cgi-bin/nonexistent.py")]
      (is (>= (:status resp) 400)))))
