(ns webserv-tests.core-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [hato.client :as hc]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "multi_location.conf"))

(def ^:private base-url "http://127.0.0.1:8080")

(deftest test-basic-get
  (testing "GET on existing resource returns 200"
    (let [resp (hc/get (str base-url "/index.html") {:throw-exceptions? false})]
      (is (= 200 (:status resp))))))

(deftest test-not-found
  (testing "GET on non-existing resource returns 4xx or 5xx (not 200)"
    (let [resp (hc/get (str base-url "/idontexist.html") {:throw-exceptions? false})]
      (is (>= (:status resp) 400)))))

(deftest test-malformed-request
  (testing "Garbage request line gets an error response or connection close"
    (let [code (server/status-code
                 (server/raw-request "127.0.0.1" 8080 "NOTAMETHOD\r\n\r\n"))]
      (is (or (nil? code) (>= code 400))))))
