#ifndef _MSCAN_HH_
#define _MSCAN_HH_

class EntryHandler {
public:
	virtual void handle(union mtrace_entry *entry) = 0;
	virtual void exit(mtrace_entry_t type) {}
private:
};

#endif // _MSCAN_HH_