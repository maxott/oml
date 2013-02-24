# Copyright (c) 2013 National ICT Australia Limited (NICTA).
# This software may be used and distributed solely under the terms of the MIT license (License).
# You should find a copy of the License in LICENSE.TXT or at http://opensource.org/licenses/MIT.
# By downloading or using this software you accept the terms and the liability disclaimer in the License.
# ------------------
#
# = benchmark.rb
#
# == Description
#
# Provides convenience functions to monitor long running services and 
# report performance metrics through OML
#
require 'oml4r'

module OML4R
  #
  # Monitor the CPU consumption of a block and report it to 
  # OML
  #
  class Benchmark

    def self.bm(label, opts = {}, &block) 
      inst = self.new(label, opts)
      inst.measure(&block) if block
      inst
    end

    # Measure execution of 'block'. Benchmarking is assumed
    # to be finished when block finished. Don't attempt to 
    # call again
    #
    def measure(&block)
      raise "Missing block" unless block
      start
      block.arity == 0 ? block.call() : block.call(self)
      _stop
    end

    # Execute block and add execution time to overall 
    # measurements. Can be called multiple time. Need
    # to finally call '#stop' to report overall stats.
    #
    def task(&block)
      raise "Missing block" unless block
      resume
      block.arity == 0 ? block.call() : block.call(self)
      pause
    end

    def start()
      @lock.synchronize do
        raise "Don't call this directly" if @running
        @running = true
      end

      if @monitor_interval > 0
        Thread.new do
          while @running 
            sleep @monitor_interval
            _report()
          end
        end
      end
      @t0, r0 = Process.times, Time.now
      @t1 = @t0
      @r1 = @r0 = r0.to_f
    end

    def pause()
      t, r = Process.times, Time.now
      @lock.synchronize do
       return unless @running
        return if @paused
        @paused = true

        @paused_t = t
        @paused_r = r.to_f
      end
    end

    def resume()
      @lock.synchronize do
        unless @running
          # hasn't been kicked off yet
          start
          return
        end

        return unless @paused
        t, r = Process.times, Time.now
        off_u = t.utime - @paused_t.utime
        off_s = t.stime - @paused_t.stime
        off_t = r.to_f - @paused_r
        @t0 = Struct::Tms.new(@t0.utime + off_u, @t0.stime + off_s)
        @r0 = @r0 + off_t
        if @t1
          @t1 = Struct::Tms.new(@t1.utime + off_u, @t1.stime + off_s)
          @r1 = @r1 + off_t
        end
        @paused = false
      end
    end

    def stop
      @lock.synchronize do
        return unless @running
      end
      _stop
    end

    # Push out an intermediate report. Does
    # nothing if already finished.
    #
    # NOTE: Not thread safe
    #
    def report(label = nil)
      return unless @running
      _report(label)
    end

    # Report a step in the processing. Used to calculate a
    # progress rate
    #
    # NOTE: Not thread safe
    #
    def step(cnt = 1)
      @step_cnt += cnt
    end

    private

    class BenchmarkMP < OML4R::MPBase
      name :benchmark
      #channel :default

      param :label
      param :note
      param :is_absolute, :type => :boolean
      param :is_final, :type => :boolean
      param :step_cnt, :type => :int32
      param :rate_real, :type => :double
      param :rate_usys, :type => :double
      param :time_wall, :type => :double
      param :time_user, :type => :double
      param :time_sys, :type => :double
    end

    def initialize(name, opts)
      @name = name
      @running = false
      @paused = false

      @monitor_interval = opts[:periodic] || -1
      @step_cnt = 0
      @step_cnt1 = 0

      @lock = Monitor.new
      @first_report = true
    end

    def _stop
      _report('done', true)
      @lock.synchronize do
        @running = false
      end
    end

    def _report(label = '-', is_final = false)
      t, r = Process.times, Time.now
      @lock.synchronize do
        if @paused
          # report ONCE while paused
          return if @paused_r == @last_paused_r
          @last_paused_r = @paused_r
          t = @paused_t
          r = @paused_r
        end
        r = r.to_f
        _inject(label, true, is_final, t, r, @t0, @r0, @step_cnt)
        unless (is_final && @first_report)
          # don't report incremental on final if this would be the first incr
          _inject(label, false, is_final, t, r, @t1, @r1, @step_cnt - @step_cnt1)
        end
        @t1 = t
        @r1 = r
        @step_cnt1 = @step_cnt
        @first_report = false
      end
    end

    def _inject(label, is_absolute, is_final, t1, r1, t0, r0, step_cnt)
      d_u = t1.utime - t0.utime
      d_s = t1.stime - t0.stime
      d_t = r1 - r0
      #puts "INJECT0 #{d_u} #{d_s} #{d_t}"
      return if (d_t <= 0 || (d_s + d_u) <= 0)
      BenchmarkMP.inject @name, label, is_absolute, is_final, step_cnt, step_cnt / d_t, step_cnt / (d_u + d_s), d_t, d_u, d_s
    end
  end
end

if __FILE__ == $0
  opts = {
    :appName => 'bm_test',
    :domain => 'foo', 
    :nodeID => 'n1',
    :collect => 'file:-'
  } 
  OML4R::init(ARGV, opts)

  bm_i = OML4R::Benchmark.bm('inner_test', periodic: 0.1)
  OML4R::Benchmark.bm('test', periodic: 0.1) do |bm|
    20.times do |i|
      10.times do
        "a" * 1_000_000
        bm.step
      end

      bm_i.task do
        10.times do
          "a" * 1_000_000
          bm_i.step
        end
      end

      sleep 0.02
      if i == 10
        bm.pause
        sleep 2
        bm.resume
      end
      #bm.report()
    end
  end
  bm_i.stop
  OML4R::close()
  puts 'done'
end

