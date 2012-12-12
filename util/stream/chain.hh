#ifndef UTIL_STREAM_CHAIN__
#define UTIL_STREAM_CHAIN__

#include "util/stream/block.hh"
#include "util/scoped.hh"

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/thread/thread.hpp>

#include <cstddef>

#include <assert.h>

namespace util {
template <class T> class PCQueue;
namespace stream {

class ChainConfigException : public Exception {
  public:
    ChainConfigException() throw();
    ~ChainConfigException() throw();
};

struct ChainConfig {
  std::size_t entry_size;
  // Chain's constructor will make thisa multiple of entry_size. 
  std::size_t block_size;
  std::size_t block_count;
  std::size_t queue_length;
};

class Chain;
// Specifies position in chain for Link constructor.
class ChainPosition {
  public:
    const Chain &GetChain() const { return *chain_; }
  private:
    friend class Chain;
    friend class Link;
    ChainPosition(PCQueue<Block> &in, PCQueue<Block> &out, Chain *chain) 
      : in_(&in), out_(&out), chain_(chain) {}

    PCQueue<Block> *in_, *out_;

    Chain *chain_;
};

class Thread {
  public:
    template <class Worker> Thread(const ChainPosition &position, const Worker &worker)
      : thread_(boost::ref(*this), position, worker) {}

    ~Thread() {
      thread_.join();
    }

    template <class Worker> void operator()(const ChainPosition &position, Worker &worker) {
      worker.Run(position);
    }

  private:
    boost::thread thread_;
};

class Recycler {
  public:
    void Run(const ChainPosition &position);
};

extern const Recycler kRecycle;

class Chain {
  private:
    template <class T, void (T::*ptr)(const ChainPosition &) = &T::Run> struct CheckForRun {
      typedef Chain type;
    };

  public:
    explicit Chain(const ChainConfig &config);

    ~Chain();

    std::size_t EntrySize() const {
      return config_.entry_size;
    }
    std::size_t BlockSize() const {
      return config_.block_size;
    }

    // Two ways to add to the chain: Add() or operator>>.  
    ChainPosition Add();

    // This is for adding threaded workers with a Run method.  
    template <class Worker> typename CheckForRun<Worker>::type &operator>>(const Worker &worker) {
      assert(!complete_called_);
      threads_.push_back(new Thread(Add(), worker));
      return *this;
    }

    // Avoid copying the worker.  
    template <class Worker> typename CheckForRun<Worker>::type &operator>>(const boost::reference_wrapper<Worker> &worker) {
      assert(!complete_called_);
      threads_.push_back(new Thread(Add(), worker));
      return *this;
    }
    
    // Note that Link and Stream also define operator>> outside this class.  

    // To complete the loop, call CompleteLoop(), >> kRecycle, or the destructor.  
    void CompleteLoop();

    Chain &operator>>(const Recycler &recycle) {
      CompleteLoop();
      return *this;
    }

  private:
    ChainConfig config_;

    scoped_malloc memory_;

    boost::ptr_vector<PCQueue<Block> > queues_;

    bool complete_called_;

    boost::ptr_vector<Thread> threads_;
};

// Create the link in the worker thread using the position token.
class Link {
  public:
    // Either default construct and Init or just construct all at once.
    Link();
    void Init(const ChainPosition &position);

    explicit Link(const ChainPosition &position);

    ~Link();

    Block &operator*() { return current_; }
    const Block &operator*() const { return current_; }

    Block *operator->() { return &current_; }
    const Block *operator->() const { return &current_; }

    Link &operator++();

    operator bool() const { return current_; }

    void Poison();

  private:
    Block current_;
    PCQueue<Block> *in_, *out_;
    bool poisoned_;
};

inline Chain &operator>>(Chain &chain, Link &link) {
  link.Init(chain.Add());
  return chain;
}

} // namespace stream
} // namespace util

#endif // UTIL_STREAM_CHAIN__