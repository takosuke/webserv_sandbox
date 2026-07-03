(ns webserv-tests.content-type-test
  "Regression tests for the missing Content-Type header and for CGI responses
  that omit a Status: line. Uses base.conf. KNOWN-FAILING items encode current
  bugs and should turn green once fixed."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [clojure.string :as str]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "base.conf"))

;; ---------------------------------------------------------------------------
;; Content-Type on static responses
;; ---------------------------------------------------------------------------
;; setup_res never emits a Content-Type, and the parsed `types` / `default_type`
;; directives are never consulted. Browsers MIME-sniff HTML but mishandle CSS,
;; JS and images without it.

(deftest test-static-html-has-content-type
  (testing "KNOWN-FAILING: a served .html file carries a text/html Content-Type header"
    (let [resp (server/http-get "/index.html")]
      (is (= 200 (:status resp)))
      (is (contains? (:headers resp) "content-type")
          "response is missing a Content-Type header entirely")
      (is (str/includes? (str (get-in resp [:headers "content-type"])) "text/html")))))

;; ---------------------------------------------------------------------------
;; CGI that omits Status: must still get a valid status line
;; ---------------------------------------------------------------------------
;; no_status.py emits only Content-Type + body. The server currently produces no
;; status line at all, yielding an invalid HTTP response instead of defaulting
;; to 200.

(deftest test-cgi-without-status-defaults-to-200
  (testing "KNOWN-FAILING: a CGI that omits Status: still yields a valid HTTP 200 status line"
    (let [resp (server/http-request
                 "GET /cgi-bin/no_status.py HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n")]
      (is (= 200 (:status resp))
          "server must supply a 200 status line when the CGI omits Status"))))
