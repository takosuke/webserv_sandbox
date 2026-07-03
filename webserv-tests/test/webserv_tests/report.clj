(ns webserv-tests.report
  "Extra kaocha reporter that annotates each failure/error with the conf file
  the server was launched with. Composed after kaocha.report/documentation in
  tests.edn, so its line prints right under each FAIL/ERROR block. Reads the
  conf name from webserv-tests.server/*conf-name*, which make-fixture binds
  around the test run."
  (:require [webserv-tests.server :as server]))

(defn conf-on-fail
  "kaocha reporter fn: on a :fail or :error event, print the active conf file."
  [m]
  (when (contains? #{:fail :error} (:type m))
    (println (str "  ↳ conf file: " (or server/*conf-name* "unknown")))))
