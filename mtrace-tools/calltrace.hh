#include <set>
#include "json.hh"

//
// Provides the current call stack via current(cpu)
//
class CallTrace : public EntryHandler {
public:
    class CallStack;

    virtual void handle(const union mtrace_entry* entry) {
        if (entry->h.type == mtrace_entry_call)
            handle(&entry->call, entry->h.cpu);
        else if (entry->h.type == mtrace_entry_fcall)
            handle(&entry->fcall, entry->h.cpu);
        else
            die("CallTrace::handle: unexpected");
    }

    CallStack* new_current(int cpu) const {
        if (current_[cpu])
            return new CallStack(*current_[cpu]);
        return NULL;
    }

    class CallStack {
        friend class CallTrace;

    public:
        JsonList* new_json(void) const {
            JsonList* list = JsonList::create();

            auto it = stack_.begin();
            for (; it != stack_.end(); ++it) {
                JsonDict* dict = JsonDict::create();
                dict->put("type", it->ret ? "ret" : "call");
                dict->put("target-pc", new JsonHex(it->target_pc));
                dict->put("target-info",
                          addr2line->function_description(it->target_pc));
                dict->put("return-pc", new JsonHex(it->return_pc));
                dict->put("return-info",
                          addr2line->function_description(it->return_pc));
                list->append(dict);
            }

            return list;
        }

    private:
        CallStack(const struct mtrace_fcall_entry* e)
            : tag_(e->tag) {}

        void push(const struct mtrace_call_entry* e) {
            stack_.push_front(*e);
        }

        void pop(const struct mtrace_call_entry* e) {
            if (!stack_.empty())
                stack_.pop_front();
        }

        const uint64_t                  tag_;
        list<struct mtrace_call_entry>  stack_;
    };

private:
    void handle(const struct mtrace_fcall_entry* e, int cpu) {
        switch (e->state) {
        case mtrace_resume: {
            auto it = call_stack_.find(e->tag);
            if (it == call_stack_.end())
                die("CallTrace::handle: missing call stack %lx", e->tag);
            current_[cpu] = it->second;
            break;
        }
        case mtrace_start: {
            CallStack* cs = new CallStack(e);
            auto it = call_stack_.find(e->tag);
            if (it != call_stack_.end())
                die("CallTrace::handle: found call stack %lx", e->tag);
            call_stack_[e->tag] = cs;
            current_[cpu] = cs;
            break;
        }
        case mtrace_pause:
            current_[cpu] = NULL;
            break;
        case mtrace_done: {
            CallStack* cs = current_[cpu];
            if (cs == NULL)
                die("CallTrace::handle: no current CallStack");
            current_[cpu] = NULL;
            call_stack_.erase(cs->tag_);
            delete cs;
            break;
        }
        default:
            die("DistinctSyscalls::handle: default error");
        }
    }

    void handle(const struct mtrace_call_entry* e, int cpu) {
        CallStack* cs;

        cs = current_[cpu];
        if (cs == NULL)
            return;

        if (e->ret)
            cs->pop(e);
        else
            cs->push(e);
    }

    CallStack*                      current_[MAX_CPUS];
    map<uint64_t, CallStack*>       call_stack_;

};

//
// Appends (to the JSON output) call traces that match the specified criteria
//
class CallTraceFilter : public EntryHandler {
    struct CallStackSummary {
        CallStackSummary(void)
            : filter_pc_(0), call_stack_() {}
        CallStackSummary(pc_t filter_pc, CallTrace::CallStack* call_stack)
            : filter_pc_(filter_pc), call_stack_(call_stack) {}
        ~CallStackSummary(void) {
            delete call_stack_;
        }

        pc_t                       filter_pc_;
        CallTrace::CallStack*      call_stack_;
    };

public:

    virtual void handle(const union mtrace_entry* entry) {
        if (entry->h.type == mtrace_entry_access)
            handle(&entry->access, entry->h.cpu);
        else
            die("CallTraceFilter::handle: unexpected");
    }

    virtual void exit(JsonDict* json_file) {
        JsonList* list = JsonList::create();

        auto it = stack_.begin();
        for (; it != stack_.end(); ++it) {
            CallStackSummary* summary;
            JsonDict* dict;

            dict = JsonDict::create();
            summary = *it;
            dict->put("call-stack", summary->call_stack_->new_json());
            dict->put("filter-pc", new JsonHex(summary->filter_pc_));
            list->append(dict);
        }
        json_file->put("call-stacks", list);
    }

    CallTraceFilter(set<pc_t> filter_pc)
        : filter_pc_(filter_pc) {}

    ~CallTraceFilter(void) {
        auto it = stack_.begin();
        for (; it != stack_.end(); ++it) {
            delete *it;
        }
    }

private:
    void handle(const struct mtrace_access_entry* a, int cpu) {
        if (filter_pc_.find(a->pc) != filter_pc_.end()) {
            CallTrace::CallStack* cs = mtrace_call_trace->new_current(a->h.cpu);
            if (cs)
                stack_.push_back(new CallStackSummary(a->pc, cs));
        }
    }

    set<pc_t> filter_pc_;
    list<CallStackSummary*> stack_;
};
