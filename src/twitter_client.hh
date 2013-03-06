#ifndef TWITTER_CLIENT_HH
#define TWITTER_CLIENT_HH
#include <tamer/tamer.hh>
#include <time.h>
#include "pqserver.hh"
#include "check.hh"
#include "json.hh"
#include "pqjoin.hh"

namespace pq {
using tamer::event;
using tamer::preevent;

typedef std::pair<const uint32_t *, const uint32_t *> FollowerRange;
typedef std::pair<int, String> RefreshResult;

// On mat, start memcached as:
//   ydmao@mat:~/install/memcached_offset/bin$ ./memcached -t 1 -m 2000 -p 11211 -B binary
template <typename C>
class HashTwitter {
  public:
    HashTwitter() {}
    template <typename R>
    void post(uint32_t author, const String &tweet, uint32_t time,
              preevent<R> e, FollowerRange *f);
    template <typename R>
    void refresh(uint32_t user, uint32_t *state, bool full, uint32_t now,
                 preevent<R, RefreshResult> e);
    // ua follow ub
    template <typename R>
    void follow(uint32_t ua, uint32_t ub, preevent<R> e);
    template <typename R>
    void stats(preevent<R, Json> e);
  private:
    C c_;
};

template <typename C> template <typename R>
void HashTwitter<C>::post(uint32_t author, const String &tweet,
                          uint32_t time, preevent<R> e, FollowerRange *fr) {
    if (!fr)
        mandatory_assert(0, "unimplemented: getting followers from memcached");
    const size_t alloclen = tweet.length() + 18;
    const size_t len = alloclen;
    char *buf = new char[alloclen];
    // store the tweet itself
    sprintf(buf, "p|%05d|%010u", author, time);
    c_.set(Str(buf, 18), tweet);
    // push to all followers
    memcpy(buf + 17, tweet.data(), tweet.length());
    buf[len - 1] = '\255';
    for (auto i = fr->first; i != fr->second; ++i) {
        // append author|time/tweet to t|*i
        String t("t|");
        t += String(*i);
        sprintf(buf, "%05u|%010u", author, time);
        buf[16] = '\254';
        c_.append(t, Str(buf, len));
    }
    delete buf;
    e();
}

template <typename C> template <typename R>
void HashTwitter<C>::refresh(uint32_t user, uint32_t *state, bool full,
                             uint32_t, preevent<R, RefreshResult> e) {
    int32_t offset = full ? 0 : *state;
    String t("t|");
    t += String(user);
    size_t value_length;
    const char *v = c_.get(t, offset, &value_length);
    int n = 0;
    Str str(v, value_length);
    for (size_t pos = 0; (pos = str.find_left('\254', pos + 1)) != -1; ++n);
    *state = offset + value_length;
    e(RefreshResult(n, String(v, value_length)));
    c_.done_get(v);
}

template <typename C> template <typename R>
void HashTwitter<C>::follow(uint32_t ua, uint32_t ub, preevent<R> e) {
    String s("s|");
    s += String(ua);
    char buf[20];
    sprintf(buf, "%05u", ub);
    c_.append(s, Str(buf, 5));
    e();
}

template <typename C> template <typename R>
void HashTwitter<C>::stats(preevent<R, Json> e) {
    e(Json());
}

template <typename S>
class PQTwitter {
  public:
    PQTwitter(bool push, bool log, S &server) : push_(push), log_(log), server_(server) {
        if (!push) {
            pq::Join* j = new pq::Join;
            auto r = j->assign_parse("t|<user_id:5>|<time:10>|<poster_id:5> "
                                     "s|<user_id>|<poster_id> "
                                     "p|<poster_id>|<time>");
            mandatory_assert(r);
            server_.add_join("t|", "t}", j);
       }
    }
    template <typename R>
    void post(uint32_t author, const String &tweet, uint32_t time,
              preevent<R> e, FollowerRange *f);
    template <typename R>
    void refresh(uint32_t user, uint32_t *state, bool full, uint32_t now,
                 preevent<R, RefreshResult> e);
    // ua follow ub
    template <typename R>
    void follow(uint32_t ua, uint32_t ub, preevent<R> e);
    template <typename R>
    void stats(preevent<R, Json> e);
  private:
    bool push_;
    bool log_;
    S &server_;
};

template <typename S> template <typename R>
void PQTwitter<S>::post(uint32_t author, const String &tweet, uint32_t time,
                        preevent<R> e, FollowerRange *fr) {
    if (!fr)
        mandatory_assert("unimplemented: getting followers from pequod");
    char buf[128];
    sprintf(buf, "p|%05d|%010u", author, time);
    if (log_)
        printf("%d: post %s\n", time, buf);
    server_.insert(Str(buf, 18), tweet);
    if (push_)
        for (auto i = fr->first; i != fr->second; ++i) {
            sprintf(buf, "t|%05u|%010u|%05u", *i, time, author);
            server_.insert(Str(buf, 24), tweet);
        }
    e();
}

template <typename S> template <typename R>
void PQTwitter<S>::refresh(uint32_t user, uint32_t *state, bool full,
                           uint32_t now, preevent<R, RefreshResult> e) {
    uint32_t tx = full ? 0 : *state;
    char buf1[128], buf2[128];
    sprintf(buf1, "t|%05d|%010d", user, tx);
    sprintf(buf2, "t|%05d}", user);
    Str first(buf1, 18);
    Str last(buf2, 8);
    server_.validate(first, last);
    auto ib = server_.lower_bound(first);
    auto ie = server_.lower_bound(last);
    int n = 0;
    String str;
    for (auto it = ib; it != ie; ++it, ++n) {
        str.append(it->key());
        char c = '\254';
        str.append(&c, 1);
        str.append(it->value());
        c = '\255';
        str.append(&c, 1);
    }
    *state = now;
    if (log_) {
        std::cout << now << ": scan [" << first << ", " << last << ")" << "\n";
        for (auto it = ib; it != ie; ++it)
            std::cout << "  " << it->key() << ": " << it->value() << "\n";
    }
    e(RefreshResult(n, str));
}

template <typename S> template <typename R>
void PQTwitter<S>::follow(uint32_t ua, uint32_t ub, preevent<R> e) {
    char buf[128];
    sprintf(buf, "s|%05d|%05d", ua, ub);
    server_.insert(Str(buf, 13), Str("1", 1));
    if (log_)
        std::cout << "subscribe " << Str(buf, 13) << "\n";
    e();
}

template <typename S> template <typename R>
void PQTwitter<S>::stats(preevent<R, Json> e) {
    e(server_.stats());
}

};

#endif
