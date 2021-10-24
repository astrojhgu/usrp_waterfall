#ifndef BUF_Q_HPP
#define BUF_Q_HPP
#include <cassert>
#include <iostream>
#include <memory>
#include <list>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <initializer_list>
#include <atomic>
template <typename T> class BufQ
{
  public:
    //filled_q stores the buffers that has been filled
    //and ready to be processed in later stage.
    std::list<std::shared_ptr<T>> filled_q;
    //unfilled_q stores the buffers that has been consumed
    //by an external consumer and marked to be ready
    //to be filled with new data
    std::list<std::shared_ptr<T>> unfilled_q;
    //proc_buf is one single buffer region that is
    //being processed/consumed by an external consumer
    std::shared_ptr<T> proc_buf = nullptr;
    std::mutex mx;
    std::condition_variable cv;
    //some external consumer is waiting for a buffer
    std::atomic_bool waiting;

  public:
    //initialize buffers
    BufQ (const std::initializer_list<std::shared_ptr<T>> &data)
    : unfilled_q{ data }, waiting (false)
    {
    }

    //initialize buffers
    BufQ (const std::vector<std::shared_ptr<T>> &data) : waiting (false)
    {
        for (auto &i : data)
            {
                unfilled_q.push_back (i);
            }
    }

    BufQ (const BufQ &) = delete;
    BufQ& operator=(const BufQ&)=delete;

  public:
    //fetch a buffer that has been filled with 
    //valid data into proc_buf.
    //then external buffer can process the data
    //in it.
    std::shared_ptr<T> fetch ()
    {
        using namespace std::chrono_literals;
        waiting.store (true);
        std::unique_lock<std::mutex> lk (mx);
        cv.wait (lk, [&, this] { return !this->filled_q.empty (); });
        waiting.store (false);
        if (proc_buf != nullptr)
            {
                unfilled_q.push_back (proc_buf);
            }
        proc_buf = filled_q.front ();
        filled_q.pop_front ();

        return proc_buf;
    }

    //Pass a closure into this method
    //this closure will fill one buffer with valid data
    //that will be later processed by an external 
    //consumer.
    //it is actually a combination of 
    //prepare_write_buf, call the func, and submit the result. 
    template <typename F> void write (F &&func)
    {
        auto p = prepare_write_buf ();

        func (p);

        submit ();
    }

    //prepare a buffer that is ready to be filled
    //with valid data.
    std::shared_ptr<T> prepare_write_buf ()
    {
        while (waiting.load () && !filled_q.empty ())
            {
                cv.notify_one ();
            }
        {
            std::lock_guard<std::mutex> lk (mx);

            // std::cout << "entering write" << std::endl;

            if (unfilled_q.empty ())
                {
                    unfilled_q.push_back (filled_q.front ());
                    filled_q.pop_front ();
                }
        }
        assert (!unfilled_q.empty ());
        return unfilled_q.front ();
    }

    //when the data filling is finished
    //call this method to move it to filled_q,
    //which is ready to be consumed.
    void submit ()
    {
        {
            std::lock_guard<std::mutex> lock (mx);
            filled_q.push_back (unfilled_q.front ());
            unfilled_q.pop_front ();
        }
        cv.notify_one ();
    }
};


#endif