(ns webserv-tests.virtual-hosts-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "virtual_hosts.conf"))

(defn- get-raw [host path]
  (server/raw-request "127.0.0.1" 8080
    (str "GET " path " HTTP/1.1\r\nHost: " host "\r\nConnection: close\r\n\r\n")))

(deftest test-default-host-serves-root
  (testing "default.local serves index.html from www/"
    (is (= 200 (server/status-code (get-raw "default.local" "/index.html"))))))

(deftest test-static-host-serves-its-root
  (testing "static.local serves index.html from www/static/"
    (is (= 200 (server/status-code (get-raw "static.local" "/index.html"))))))

(deftest test-unknown-host-falls-back-to-default
  (testing "Unrecognised Host header falls back to the default server"
    (is (= 200 (server/status-code (get-raw "unknown.local" "/index.html"))))))

(deftest test-static-host-missing-file
  (testing "static.local returns 404 for a file that only exists in www/ not www/static/"
    (is (= 404 (server/status-code (get-raw "static.local" "/404.html"))))))
