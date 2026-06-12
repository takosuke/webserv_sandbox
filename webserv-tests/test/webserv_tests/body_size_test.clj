(ns webserv-tests.body-size-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "body_size_limit.conf"))

(deftest test-normal-get-unaffected
  (testing "GET requests are not affected by the body size limit"
    (let [resp (server/http-get "/index.html")]
      (is (not= 413 (:status resp))))))

(defn- post-raw [body]
  (let [payload (str "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: "
                     (count body) "\r\n\r\n" body)]
    (server/status-code (server/raw-request "127.0.0.1" 8080 payload))))

(deftest test-small-body-accepted
  (testing "POST with body under client_max_body_size is not rejected with 413"
    (is (not= 413 (post-raw "small")))))

;; Known server limitation: client_max_body_size enforcement is parsed but not
;; yet checked in the request body reader.  This test documents the desired
;; behaviour; it will pass once the server enforces the limit.
(deftest test-oversized-body-rejected
  (testing "POST with body exceeding client_max_body_size (100 bytes) returns 413"
    (is (= 413 (post-raw (apply str (repeat 200 "X")))))))
