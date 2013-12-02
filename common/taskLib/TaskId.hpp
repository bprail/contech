#ifndef TASK_ID_HPP
#define TASK_ID_HPP

#include <inttypes.h>
#include <iostream>
#include <sstream>

namespace contech {

class ContextId
{
private:
    uint32_t id;
public:
    ContextId() : id(0) {}
    ContextId(uint32_t id_) : id(id_) {}
    explicit operator uint32_t() const { return id; }
    bool operator==(const ContextId& rhs) const { return id == rhs.id; }
    bool operator!=(const ContextId& rhs) const { return id != rhs.id; }
    bool operator<(const ContextId& rhs) const { return id < rhs.id; }
    bool operator>(const ContextId& rhs) const { return id > rhs.id; }
    bool operator>=(const ContextId& rhs) const { return id >= rhs.id; }
    bool operator<=(const ContextId& rhs) const { return id <= rhs.id; }
    friend class TaskId;
    std::string toString() const { return std::to_string(id); }
};
inline static std::ostream& operator<<(std::ostream& os, const ContextId& i) { os << i.toString(); return os; }

class SeqId
{
private:
    uint32_t id;
public:
    SeqId() : id(0) {}
    SeqId(uint32_t id_) : id(id_) {}
    explicit operator uint32_t() const { return id; }
    SeqId getNext() const { return SeqId(id + 1); }
    bool operator==(const SeqId& rhs) const { return id == rhs.id; }
    bool operator!=(const SeqId& rhs) const { return id != rhs.id; }
    bool operator<(const SeqId& rhs) const { return id < rhs.id; }
    bool operator>(const SeqId& rhs) const { return id > rhs.id; }
    bool operator>=(const SeqId& rhs) const { return id >= rhs.id; }
    bool operator<=(const SeqId& rhs) const { return id <= rhs.id; }
    friend class TaskId;
    std::string toString() const { return std::to_string(id); }
};
inline static std::ostream& operator<<(std::ostream& os, const SeqId& i) { os << i.toString(); return os; }

class TaskId
{
private:
    uint64_t id;
public:
    TaskId() : id(0) {}
    TaskId(uint64_t id_) : id(id_) {}
    TaskId(ContextId cid, SeqId sid) : id((uint64_t)cid.id<<32 | sid.id) {}
    explicit operator uint64_t() const { return id; }
    ContextId getContextId() const { return ContextId(id>>32); }
    SeqId getSeqId() const { return SeqId((uint32_t)id); }
    TaskId getNext() const { return TaskId(id + 1); }
    bool operator==(const TaskId& rhs) const { return id == rhs.id; }
    bool operator!=(const TaskId& rhs) const { return id != rhs.id; }
    bool operator<(const TaskId& rhs) const { return id < rhs.id; }
    bool operator>(const TaskId& rhs) const { return id > rhs.id; }
    bool operator>=(const TaskId& rhs) const { return id >= rhs.id; }
    bool operator<=(const TaskId& rhs) const { return id <= rhs.id; }
    friend class std::hash<contech::TaskId>;
    std::string toString() const { return getContextId().toString() + ":" + getSeqId().toString(); }
};
inline static std::ostream& operator<<(std::ostream& os, const TaskId& i) { os << i.toString(); return os; }

} // end namespace contech

// overload std::hash for TaskId so it can be used in hash maps
namespace std
{
    template <>
    struct hash<contech::TaskId>
    {
        typedef size_t result_type;
        typedef contech::TaskId argument_type;

        result_type operator()(contech::TaskId const & in) const noexcept
        {
            return hash<uint64_t>()(in.id);
        }
    };
}

#endif
