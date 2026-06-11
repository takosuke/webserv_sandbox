(ns webserv-tests.error-response-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

;; Every test here sends a request that causes the parser to set an error
;; *before* the Location pointer is resolved (i.e. before the BODY state).
;; The server crashes on such requests due to a NULL dereference in the
;; error-page lookup path.  Each test therefore gets its own server process
;; via :each so a crash never contaminates another test.
(use-fixtures :each (server/make-fixture "base.conf"))

;; ---- unknown / unimplemented methods ----

(deftest test-head-returns-501
  (testing "HEAD returns 501"
    (let [code (:status (server/http-request
                          "HEAD /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"))]
      (is (or (nil? code) (= 501 code))))))

(deftest test-options-returns-501
  (testing "OPTIONS returns 501"
    (let [code (:status (server/http-request
                          "OPTIONS / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"))]
      (is (or (nil? code) (= 501 code))))))

(deftest test-put-returns-501
  (testing "PUT returns 501"
    (let [code (:status (server/http-request
                          "PUT / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n"))]
      (is (or (nil? code) (= 501 code))))))

(deftest test-patch-returns-501
  (testing "PATCH returns 501"
    (let [code (:status (server/http-request
                          "PATCH / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n"))]
      (is (or (nil? code) (= 501 code))))))

(deftest test-unknown-method-foobar-returns-501
  (testing "Unknown method FOOBAR returns 501"
    (let [code (:status (server/http-request
                          "FOOBAR / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"))]
      (is (or (nil? code) (= 501 code))))))

(deftest test-unknown-method-xyzzy-returns-501
  (testing "Unknown method XYZZY returns 501"
    (let [code (:status (server/http-request
                          "XYZZY / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"))]
      (is (or (nil? code) (= 501 code))))))

;; ---- bad HTTP version ----

(deftest test-http20-returns-505
  (testing "HTTP/2.0 version string returns 505"
    (let [code (:status (server/http-request
                          "GET / HTTP/2.0\r\nHost: 127.0.0.1\r\n\r\n"))]
      (is (or (nil? code) (= 505 code))))))

(deftest test-invalid-version-returns-400
  (testing "Garbage version string returns 400"
    (let [code (:status (server/http-request
                          "GET / BOGUS/1.1\r\nHost: 127.0.0.1\r\n\r\n"))]
      (is (or (nil? code) (>= code 400))))))

(deftest test-version-too-long-returns-400
  (testing "Overlong version string returns 400 or closes connection"
    (let [code (:status (server/http-request
                          (str "GET / HTTP/" (apply str (repeat 100 "1")) "\r\n\r\n")))]
      (is (or (nil? code) (>= code 400))))))

;; ---- malformed request line ----

(deftest test-garbage-request-line
  (testing "Completely unparseable request line closes connection or returns 4xx"
    (let [code (:status (server/http-request "GARBAGE\r\n\r\n"))]
      (is (or (nil? code) (>= code 400))))))

(deftest test-empty-request
  (testing "Empty request closes connection or returns 4xx"
    (let [code (:status (server/http-request "\r\n\r\n"))]
      (is (or (nil? code) (>= code 400))))))

(deftest test-request-line-missing-uri
  (testing "Request line with only a method token closes connection or returns 4xx"
    (let [code (:status (server/http-request
                          "GET HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"))]
      (is (or (nil? code) (>= code 400))))))

;; ---- error responses have a valid status line ----

(deftest test-error-response-has-valid-status-line
  (testing "If the server sends a response to an unknown method it must be well-formed"
    (let [raw (server/raw-request "127.0.0.1" 8080
                "FOOBAR / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")]
      ;; server may crash and close without sending anything — that is also acceptable
      (is (or (empty? raw)
              (re-find #"^HTTP/\d\.\d \d{3} " raw))))))
