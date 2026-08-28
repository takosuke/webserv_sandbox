(ns webserv-tests.limit-except-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "limit_except.conf"))

(deftest test-get-allowed-on-restricted-location
  (testing "GET on a limit_except GET location is permitted (not 405)"
    (is (not= 405 (server/status-code
                    (server/raw-request "127.0.0.1" 8080
                      "GET /readonly HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"))))))

(deftest test-post-blocked-on-restricted-location
  (testing "POST on a limit_except GET location returns 405"
    (is (= 405 (server/status-code
                 (server/raw-request "127.0.0.1" 8080
                   "POST /readonly HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n"))))))

(deftest test-delete-blocked-on-restricted-location
  (testing "DELETE on a limit_except GET location returns 405"
    (is (= 405 (server/status-code
                 (server/raw-request "127.0.0.1" 8080
                   "DELETE /readonly HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n"))))))

;; RFC 9110 15.5.6: "The origin server MUST generate an Allow header field in a
;; 405 response containing a list of the target resource's currently supported
;; methods." response-format-test pins the negative case on a 200.

(deftest test-405-carries-allow-header
  (testing "a 405 response carries an Allow header listing the permitted methods"
    (let [resp (server/http-request
                 "POST /readonly HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")]
      (is (= 405 (:status resp)))
      (is (contains? (:headers resp) "allow")
          "a 405 must advertise the methods the location does allow")
      (is (clojure.string/includes? (get (:headers resp) "allow") "GET")
          "the limit_except GET location allows GET"))))
