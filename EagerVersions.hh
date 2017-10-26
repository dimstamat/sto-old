#pragma once

// This file contains definition of classes TLockVersion and TSwissVersion
// They both perform eager (or pessimistic) write-write concurrency control and hence
// the file name

#include "VersionBase.hh"
#include "TThread.hh"

#ifndef ADAPTIVE_RWLOCK
#define ADAPTIVE_RWLOCK 0
#endif

enum class LockResponse : int {locked, failed, optimistic, spin};

class TLockVersion : public BasicVersion<TLockVersion> {
public:
    typedef TransactionTid::type type;
    static constexpr type mask = TransactionTid::threadid_mask;
    static constexpr type rlock_cnt_max = type(0x10); // max number of readers
    static constexpr type lock_bit = TransactionTid::lock_bit;
    static constexpr type opt_bit = TransactionTid::opt_bit;

    using BV = BasicVersion<TLockVersion>;
    using BV::v_;

    TLockVersion() = default;
    explicit TLockVersion(type v)
            : BasicVersion<TLockVersion>(v) {}
    TLockVersion(type v, bool insert)
            : BasicVersion<TLockVersion>(v) {(void)insert;}

    bool cp_try_lock_impl(int threadid) {
        (void)threadid;
        return (try_lock_write() == LockResponse::locked);
    }

    void cp_unlock_impl(TransItem& item) {
        assert(item.needs_unlock());
        if (item.has_write())
            unlock_write();
        else {
            assert(item.has_read());
            unlock_read();
        }
    }

    inline bool acquire_write_impl(TransItem& item);
    template <typename T>
    inline bool acquire_write_impl(TransItem& item, const T& wdata);
    template <typename T>
    inline bool acquire_write_impl(TransItem& item, T&& wdata);
    template <typename T, typename... Args>
    inline bool acquire_write_impl(TransItem& item, Args&&... args);

    inline bool observe_read_impl(TransItem& item, bool add_read);

    static inline type& cp_access_tid_impl(Transaction& txn);
    inline type cp_commit_tid_impl(Transaction& txn);

    bool hint_optimistic() const {
        return (v_ & opt_bit) != 0;
    }

private:
    // read/writer/optimistic combined lock
    std::pair<LockResponse, type> try_lock_read() {
        while (true) {
            type vv = v_;
            bool write_locked = ((vv & lock_bit) != 0);
            type rlock_cnt = vv & mask;
            bool rlock_avail = rlock_cnt < rlock_cnt_max;
            if (write_locked)
                return std::make_pair(LockResponse::spin, type());
            if (!rlock_avail)
                return std::make_pair(LockResponse::optimistic, vv);
            if (::bool_cmpxchg(&v_, vv, (vv & ~mask) | (rlock_cnt+1)))
                return std::make_pair(LockResponse::locked, type());
            else
                relax_fence();
        }
    }

    LockResponse try_lock_write() {
        while (true) {
            type vv = v_;
            bool write_locked = ((vv & lock_bit) != 0);
            bool read_locked = ((vv & mask) != 0);
            if (write_locked || read_locked)
                return LockResponse::spin;
            //if (read_locked)
            //    return LockResponse::spin;
            if (::bool_cmpxchg(&v_, vv,
#if ADAPTIVE_RWLOCK == 0
                             (vv | lock_bit)
#else
                    (vv | lock_bit) & ~opt_bit
#endif
            )) {
                return LockResponse::locked;
            }
            else
                relax_fence();
        }
    }

    LockResponse try_upgrade() {
        // XXX not used
        type vv = v_;
        type rlock_cnt = vv & mask;
        assert(!TransactionTid::is_locked(vv));
        assert(rlock_cnt >= 1);
        if ((rlock_cnt == 1) && ::bool_cmpxchg(&v_, vv, (vv - 1) | lock_bit))
            return LockResponse::locked;
        else
            return LockResponse::spin;
    }

    void unlock_read() {
#if ADAPTIVE_RWLOCK == 0
        type vv = __sync_fetch_and_add(&v_, -1);
        (void)vv;
        assert((vv & mask) >= 1);
#else
        while (1) {
            type vv = v_;
            assert((vv & threadid_mask) >= 1);
            type new_v = TThread::gen[TThread::id()].chance(unlock_opt_chance) ?
                    ((vv - 1) | opt_bit) : (vv - 1);
            if (::bool_cmpxchg(&v_, vv, new_v))
                break;
            relax_fence();
        }
#endif
    }

    void unlock_write() {
        assert(is_locked());
#if ADAPTIVE_RWLOCK == 0
        type new_v = v_ & ~lock_bit;
#else
        type new_v = TThread::gen[TThread::id()].chance(unlock_opt_chance) ?
                ((v_ & ~lock_bit) | opt_bit) : (v_ & ~lock_bit);
#endif
        v_ = new_v;
        release_fence();
    }

    inline bool try_upgrade_with_spin();
    inline bool try_lock_write_with_spin();
    inline std::pair<LockResponse, type> try_lock_read_with_spin();
    inline bool lock_for_write(TransItem& item);

#if ADAPTIVE_RWLOCK != 0
    // hacky state used by adaptive read/write lock
    static int unlock_opt_chance;
#endif
};

template <bool Opacity>
class TSwissVersion : public BasicVersion<TSwissVersion<Opacity>> {
public:
    typedef TransactionTid::type type;
    static constexpr bool is_opaque = Opacity;
    static constexpr type lock_bit = TransactionTid::lock_bit;
    static constexpr type threadid_mask = TransactionTid::threadid_mask;
    static constexpr type read_lock_bit = TransactionTid::dirty_bit;

    using BV = BasicVersion<TSwissVersion<Opacity>>;
    using BV::v_;

    TSwissVersion()
            : BV(Opacity ? TransactionTid::nonopaque_bit : 0) {}
    explicit TSwissVersion(type v, bool insert = true)
            : BV(v | (insert ? (lock_bit | TThread::id()) : 0)) {
        if (Opacity)
            v_ |= TransactionTid::nonopaque_bit;
    }

    // commit-time lock for TSwissVersion is just setting the read-lock (dirty) bit
    bool cp_try_lock_impl(int threadid) {
        (void)threadid;
        assert(BV::is_locked_here(threadid));
        v_ |= read_lock_bit;
        release_fence();
        return true;
    }
    void cp_unlock_impl(TransItem& item) {
        (void)item;
        assert(item.needs_unlock());
        if (BV::is_locked())
            TransactionTid::unlock(v_);
    }

    inline bool acquire_write_impl(TransItem& item);
    template <typename T>
    inline bool acquire_write_impl(TransItem& item, const T& wdata);
    template <typename T>
    inline bool acquire_write_impl(TransItem& item, T&& wdata);
    template <typename T, typename... Args>
    inline bool acquire_write_impl(TransItem& item, Args&&... args);

    inline bool observe_read_impl(TransItem& item, bool add_read);

    static inline type& cp_access_tid_impl(Transaction& txn);
    inline type cp_commit_tid_impl(Transaction& txn);

private:
    bool try_lock() {
        return TransactionTid::try_lock(v_, TThread::id());
    }

    bool is_read_locked() const {
        return (v_ & read_lock_bit) != 0;
    }
};