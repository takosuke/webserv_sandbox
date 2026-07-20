#pragma once

class PidCollector {
private:
  std::set<pid_t> _pids;

  PidCollector();
  PidCollector(PidCollector const & other);
  ~PidCollector();

  PidCollector & operator=(PidCollector const & other);

public:
  PidCollector & get_instance();


}
