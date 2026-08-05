(ns webserv-tests.cgi-lifecycle-test
  "Process-lifecycle tests for forked CGI children.

  These cover the deferred-reaper design that replaced the blocking
  waitpid(pid, NULL, 0) in finalize_cgi(): EpollLoop keeps a pid -> deadline
  registry, sweeps it once per tick with WNOHANG, and escalates SIGTERM ->
  SIGKILL on any child that outlives its deadline. ClientConnection registers
  the child in setup_cgi(), detaches in finalize_cgi() (the response is already
  staged on disk and never depended on the exit status), and hands a still-live
  child to the registry from its destructor when the client goes away first.

  Where the rest of the suite asserts on what comes back over the socket, this
  namespace asserts on the process table and /proc: a server can answer every
  request correctly while still leaking a child per request, and only an
  outside view catches that. Three failure modes are pinned here, each of which
  the suite was previously blind to:

    - a runaway CGI is never bounded, so the connection hangs forever
      (subject: 'a request to your server should never hang indefinitely');
    - the escalation is logged but not performed, so a CGI that ignores
      SIGTERM survives the server;
    - children forked for a client that disconnected are never reaped, so
      zombies and their fds accumulate for the process lifetime.

  Timing: the deadline is a compile-time constant, so these tests are slow by
  construction — budget ~15 s for each kill assertion. See cgi-timeout-ms."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(defn- reap-strays
  "Runs inside make-fixture, so its cleanup happens while the server is still
  alive. Every test here can leave a CGI running — on failure, or simply
  because a deadline had not elapsed yet — and stop! is a SIGKILL, which gives
  ~EpollLoop no chance to clean up after itself. An orphaned CGI holds the
  inherited listen socket, so the port stays bound and every namespace after
  this one fails to start its server. Kill them here instead."
  [run-tests]
  (try (run-tests)
       (finally (server/kill-stray-children!))))

(use-fixtures :once (server/make-fixture "base.conf") reap-strays)

;; ---------------------------------------------------------------------------
;; Timing constants
;; ---------------------------------------------------------------------------
;; These mirror compile-time #defines; there is no way to set them from a conf
;; file, so they have to be kept in step by hand. If the tests below start
;; failing on their deadlines after a header change, check these first.
;;
;;   CGI_TIMEOUT     inc/ClientConnection.hpp   seconds a CGI may run
;;   CGI_KILL_GRACE  inc/EpollLoop.hpp          seconds between TERM and KILL

(def ^:private cgi-timeout-ms 10000)
(def ^:private kill-grace-ms   2000)

;; Slack on top of the true deadline: the sweep runs on the loop tick, and CI
;; machines are not real-time. Generous enough not to flake, far short of the
;; "never" these tests exist to rule out.
(def ^:private slack-ms 5000)

(def ^:private terminate-deadline-ms (+ cgi-timeout-ms slack-ms))
(def ^:private escalate-deadline-ms  (+ cgi-timeout-ms kill-grace-ms slack-ms))

(defn- cgi-get [path]
  (str "GET " path " HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n"))

(defn- quiesce!
  "Wait for the server to own no child processes. Tests in this namespace share
  one server (:once), so each starts from a known-clean process table instead
  of inheriting the previous test's dying children."
  []
  (server/wait-until (+ escalate-deadline-ms 5000)
                     #(empty? (server/child-pids))))

;; ---------------------------------------------------------------------------
;; Control: the ordinary path must not regress
;; ---------------------------------------------------------------------------
;; The reaper is only correct if it also stays out of the way. A CGI that exits
;; on its own must be answered normally and must not linger as a zombie waiting
;; for someone to call waitpid.

(deftest test-normal-cgi-is-answered-and-reaped
  (testing "a well-behaved CGI answers 200 and leaves no child behind (control)"
    (quiesce!)
    ;; no_status.py, not hello.py: hello.py is a fixture that deliberately
    ;; emits "Status: 400" to exercise CGI status handling elsewhere.
    (let [{:keys [response timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080 (cgi-get "/cgi-bin/no_status.py") 5000)]
      (is (not timed-out) "a normal CGI request must complete")
      (is (= 200 (server/status-code response))))
    (is (server/wait-until 5000 #(empty? (server/child-pids)))
        (str "a CGI that exited on its own was never reaped; still tracked: "
             (server/child-pids)))))

;; ---------------------------------------------------------------------------
;; A runaway CGI is bounded by its deadline
;; ---------------------------------------------------------------------------
;; hang_forever.py never writes and never exits. Nothing on the connection side
;; can bound it: _timeout only advances the sweep when data arrives, and no data
;; ever arrives. If the process deadline does not fire, this request hangs for
;; as long as the server runs.

(deftest test-runaway-cgi-is-terminated-at-its-deadline
  (testing "a CGI that never exits is killed at CGI_TIMEOUT and the request is answered"
    (quiesce!)
    (let [start (System/currentTimeMillis)
          {:keys [response timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            (cgi-get "/cgi-bin/hang_forever.py") terminate-deadline-ms)
          elapsed (- (System/currentTimeMillis) start)]
      (is (not timed-out)
          (str "the request was still open after " elapsed
               " ms; a runaway CGI must be bounded by its deadline, not left to hang"))
      (is (some? (server/status-code response))
          "killing the CGI must produce a real status line, not a bare close")
      (is (>= elapsed (- cgi-timeout-ms 1000))
          (str "answered after only " elapsed " ms — earlier than CGI_TIMEOUT, so "
               "something other than the deadline ended this request")))
    (is (server/wait-until 5000 #(empty? (server/child-pids)))
        "the killed CGI must also be reaped, not left as a zombie")))

(deftest test-server-stays-responsive-while-a-cgi-runs-away
  (testing "a runaway CGI does not stall the event loop for other clients"
    (quiesce!)
    (let [runaway (future (server/raw-request-timeout "127.0.0.1" 8080
                            (cgi-get "/cgi-bin/hang_forever.py")
                            terminate-deadline-ms))]
      (try
        (Thread/sleep 500)
        (is (server/responsive? 2000)
            "an unrelated client must be served while a CGI is stuck")
        (finally
          @runaway
          (quiesce!))))))

;; ---------------------------------------------------------------------------
;; SIGTERM -> SIGKILL escalation
;; ---------------------------------------------------------------------------
;; sigterm_immune.py ignores SIGTERM. A reaper that sends only the polite signal
;; — or that logs the escalation without performing it — leaves this process
;; running after the server itself is gone. That is the one failure mode the
;; log cannot reveal, because the log claims success either way.

(deftest test-sigterm-immune-cgi-is-escalated-to-sigkill
  (testing "a CGI that ignores SIGTERM is escalated to SIGKILL and dies"
    (quiesce!)
    (let [start (System/currentTimeMillis)
          {:keys [timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            (cgi-get "/cgi-bin/sigterm_immune.py") escalate-deadline-ms)
          elapsed (- (System/currentTimeMillis) start)]
      (is (not timed-out)
          (str "still waiting after " elapsed
               " ms; SIGTERM was ignored and SIGKILL never arrived"))
      (is (>= elapsed cgi-timeout-ms)
          "the child cannot have died before its deadline"))
    (is (server/wait-until 5000 #(empty? (server/child-pids)))
        (str "a SIGTERM-immune CGI outlived the escalation; still alive: "
             (server/child-pids)))))

;; ---------------------------------------------------------------------------
;; Client abort: children must not outlive the connection that forked them
;; ---------------------------------------------------------------------------
;; After setup_cgi the connection watches the CGI pipe, not the client socket,
;; so a client that walks away is not noticed at all. The child is only cleaned
;; up because ClientConnection's destructor hands it to the registry. Without
;; that hand-off every aborted CGI request leaks a process and its fds for the
;; lifetime of the server.

(deftest test-aborted-cgi-requests-leave-no-zombies
  (testing "children forked for clients that disconnected are killed and reaped"
    (quiesce!)
    (dotimes [_ 10]
      (server/abort-request "127.0.0.1" 8080 (cgi-get "/cgi-bin/hang_forever.py") 100))
    (is (seq (server/child-pids))
        "sanity: the aborted requests should have forked children in the first place")
    (is (server/wait-until (+ escalate-deadline-ms 5000)
                           #(empty? (server/child-pids)))
        (str "abandoned CGI children were never cleaned up; "
             (count (server/child-pids)) " still tracked, of which "
             (count (server/zombie-pids)) " are zombies"))
    (is (empty? (server/zombie-pids))
        "no child may be left in state Z")))

(deftest test-aborted-cgi-requests-do-not-leak-fds
  (testing "an aborted CGI request releases its socket and both pipe ends"
    (quiesce!)
    (let [before (server/open-fd-count)]
      (dotimes [_ 10]
        (server/abort-request "127.0.0.1" 8080 (cgi-get "/cgi-bin/hang_forever.py") 100))
      (quiesce!)
      ;; Give the loop a tick past the last reap to close what it owns.
      (let [settled (server/wait-until 5000 #(when (<= (server/open-fd-count) before) true))
            after   (server/open-fd-count)]
        (is settled
            (str "open fds went from " before " to " after
                 " across 10 aborted CGI requests and did not come back down; "
                 "each abort leaks a client socket and/or a CGI pipe"))))
    (is (server/responsive? 2000)
        "the server must still be serving after the aborts")))
