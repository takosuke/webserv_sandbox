(ns webserv-tests.request-line-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

;; Only valid-path requests that complete successfully.
;; Tests expecting 4xx/5xx from request-line parsing are in error_response_test.clj
;; (they crash the server before location is resolved and must use :each).
(use-fixtures :once (server/make-fixture "base.conf"))

(deftest test-http11-get
  (testing "Well-formed HTTP/1.1 GET returns 200"
    (is (= 200 (:status (server/http-get "/index.html"))))))

(deftest test-http10-get
  (testing "HTTP/1.0 GET (no Host header required) returns 200"
    (is (= 200 (:status (server/http-request
                          "GET /index.html HTTP/1.0\r\n\r\n"))))))
