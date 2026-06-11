(ns webserv-tests.body-size-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [hato.client :as hc]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "body_size_limit.conf"))

(def ^:private base-url "http://127.0.0.1:8080")

(deftest test-normal-get-unaffected
  (testing "GET requests are not affected by the body size limit"
    (let [resp (hc/get (str base-url "/index.html") {:throw-exceptions? false})]
      (is (not= 413 (:status resp))))))

(defn- post-raw [body]
  (let [payload (str "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: "
                     (count body) "\r\n\r\n" body)]
    (server/status-code (server/raw-request "127.0.0.1" 8080 payload))))

(deftest test-small-body-accepted
  (testing "POST with body under client_max_body_size is not rejected with 413"
    (is (not= 413 (post-raw "small")))))

(deftest test-oversized-body-rejected
  (testing "POST with body exceeding client_max_body_size (100 bytes) returns 413"
    (is (= 413 (post-raw (apply str (repeat 200 "X")))))))
