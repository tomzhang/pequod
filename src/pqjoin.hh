#ifndef PEQUOD_PQJOIN_HH
#define PEQUOD_PQJOIN_HH 1
#include <stdint.h>
#include <string.h>
#include "str.hh"
template <typename K, typename V> class HashTable;
class Json;
class String;

namespace pq {
class Join;

enum { slot_capacity = 5 };

class Match {
  public:
    class state {
      private:
        uint8_t slotlen_[(slot_capacity + 3) & ~3];
        friend class Match;
    };

    inline Match();

    inline int slotlen(int i) const;
    inline const uint8_t* slot(int i) const;
    inline void set_slot(int i, const uint8_t* data, int len);
    Match& operator&=(const Match& m);

    inline const state& save() const;
    inline void restore(const state& state);

    friend std::ostream& operator<<(std::ostream&, const Match&);

  private:
    state ms_;
    const uint8_t* slot_[slot_capacity];
};

class Pattern {
  public:
    inline Pattern();

    void append_literal(uint8_t ch);
    void append_slot(int si, int len);

    inline int key_length() const;
    bool has_slot(int si) const;

    inline bool match(Str str) const;
    inline bool match(Str str, Match& m) const;
    inline bool match_complete(const Match& m) const;
    inline int first(uint8_t* s, const Match& m) const;
    inline int last(uint8_t* s, const Match& m) const;
    inline void expand(uint8_t* s, const Match& m) const;

    bool assign_parse(Str str, HashTable<Str, int> &slotmap);
    bool assign_parse(Str str);

    Json unparse_json() const;
    String unparse() const;

  private:
    enum { pcap = 12 };
    uint8_t plen_;
    uint8_t klen_;
    uint8_t pat_[pcap];
    uint8_t slotlen_[slot_capacity];
    uint8_t slotpos_[slot_capacity];

    friend class Join;
};

class Join {
  public:
    inline Join();

    inline void ref();
    inline void deref();

    inline int size() const;
    inline const Pattern& operator[](int i) const;
    inline const Pattern& back() const;
    inline void expand(uint8_t* out, Str str) const;

    bool assign_parse(Str str);

    Json unparse_json() const;
    String unparse() const;

  private:
    enum { pcap = 5 };
    int npat_;
    Pattern pat_[pcap];
    int refcount_;
};


inline Match::Match() {
    memset(ms_.slotlen_, 0, sizeof(ms_.slotlen_));
}

inline int Match::slotlen(int i) const {
    return ms_.slotlen_[i];
}

inline const uint8_t* Match::slot(int i) const {
    return slot_[i];
}

inline void Match::set_slot(int i, const uint8_t* data, int len) {
    slot_[i] = data;
    ms_.slotlen_[i] = len;
}

inline const Match::state& Match::save() const {
    return ms_;
}

inline void Match::restore(const state& state) {
    ms_ = state;
}

inline Pattern::Pattern()
    : plen_(0), klen_(0) {
}

inline int Pattern::key_length() const {
    return klen_;
}

inline bool Pattern::match(Str str) const {
    if (str.length() != key_length())
	return false;
    const uint8_t* ss = str.udata(), *ess = str.udata() + str.length();
    for (const uint8_t* p = pat_; p != pat_ + plen_ && ss != ess; ++p)
	if (*p < 128) {
	    if (*ss != *p)
		return false;
	    ++ss;
	} else
	    ss += slotlen_[*p - 128];
    return true;
}

inline bool Pattern::match(Str s, Match& m) const {
    const uint8_t* ss = s.udata(), *ess = s.udata() + s.length();
    for (const uint8_t* p = pat_; p != pat_ + plen_ && ss != ess; ++p)
	if (*p < 128) {
	    if (*ss != *p)
		return false;
	    ++ss;
	} else {
	    int slotlen = m.slotlen(*p - 128);
	    if (slotlen) {
		if (slotlen > ess - ss)
		    slotlen = ess - ss;
		if (memcmp(ss, m.slot(*p - 128), slotlen) != 0)
		    return false;
	    }
	    if (slotlen < slotlen_[*p - 128] && slotlen < ess - ss) {
		slotlen = slotlen_[*p - 128];
		if (slotlen > ess - ss)
		    slotlen = ess - ss;
		m.set_slot(*p - 128, ss, slotlen);
	    }
	    ss += slotlen;
	}
    return true;
}

inline bool Pattern::match_complete(const Match& m) const {
    for (const uint8_t* p = pat_; p != pat_ + plen_; ++p)
	if (*p >= 128 && m.slotlen(*p - 128) != slotlen_[*p - 128])
	    return false;
    return true;
}

inline int Pattern::first(uint8_t* s, const Match& m) const {
    uint8_t* first = s;
    for (const uint8_t* p = pat_; p != pat_ + plen_; ++p)
	if (*p < 128) {
	    *s = *p;
	    ++s;
	} else {
	    int slotlen = m.slotlen(*p - 128);
	    if (slotlen) {
		memcpy(s, m.slot(*p - 128), slotlen);
		s += slotlen;
	    }
	    if (slotlen != slotlen_[*p - 128])
		break;
	}
    return s - first;
}

inline int Pattern::last(uint8_t* s, const Match& m) const {
    uint8_t* first = s;
    for (const uint8_t* p = pat_; p != pat_ + plen_; ++p)
	if (*p < 128) {
	    *s = *p;
	    ++s;
	} else {
	    int slotlen = m.slotlen(*p - 128);
	    if (slotlen) {
		memcpy(s, m.slot(*p - 128), slotlen);
		s += slotlen;
	    } else {
		while (s != first) {
		    ++s[-1];
		    if (s[-1] != 0)
			break;
		    --s;
		}
	    }
	    if (slotlen != slotlen_[*p - 128])
		break;
	}
    return s - first;
}

inline void Pattern::expand(uint8_t* s, const Match& m) const {
    for (const uint8_t* p = pat_; p != pat_ + plen_; ++p)
	if (*p < 128) {
	    *s = *p;
	    ++s;
	} else {
	    int slotlen = m.slotlen(*p - 128);
	    if (slotlen)
		memcpy(s, m.slot(*p - 128), slotlen);
	    s += slotlen_[*p - 128];
	}
}

inline Join::Join()
    : npat_(0), refcount_(0) {
}

inline void Join::ref() {
    ++refcount_;
}

inline void Join::deref() {
    if (--refcount_ == 0)
	delete this;
}

inline int Join::size() const {
    return npat_;
}

inline const Pattern& Join::operator[](int i) const {
    return pat_[i];
}

inline const Pattern& Join::back() const {
    return pat_[npat_ - 1];
}

inline void Join::expand(uint8_t* s, Str str) const {
    const Pattern& last = back();
    const Pattern& first = pat_[0];
    for (const uint8_t* p = last.pat_; p != last.pat_ + last.plen_; ++p)
	if (*p >= 128)
	    memcpy(s + first.slotpos_[*p - 128],
		   str.udata() + last.slotpos_[*p - 128],
		   last.slotlen_[*p - 128]);
}

std::ostream& operator<<(std::ostream&, const Match&);

} // namespace
#endif