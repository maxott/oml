#
# Copyright 2009-2012 National ICT Australia (NICTA), Australia
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
#
# = oml4r.rb
#
# == Description
#
# This is a simple client library for OML which does not use liboml2 and its
# filters, but connects directly to the server using the +text+ protocol.
# User can use this library to create ruby applications which can send
# measurement to the OML collection server.
#
require 'socket'
require 'monitor'
require 'thread'

#
# This is the OML4R module, which should be required by ruby applications
# that want to collect measurements via OML
#
module OML4R

  VERSION = "2.8.pre0"
  REVISION = "$Revision: 1 $".split(":")[1].chomp("$").strip
  VERSION_STRING = "OML4R Client Library - Version #{VERSION} (#{REVISION})"
  DEF_SERVER_PORT = 3003

  #
  # Measurement Point Class
  # Ruby applications using this module should sub-class this MPBase class
  # to define their own Measurement Point (see the example at the end of
  # this file)
  #
  class MPBase

    # Some Class variables
    @@defs = {}
    @@channels = {}
    @@frozen = false
    @@useOML = false
    @@start_time = nil

    # Execute a block for each defined MP
    def self.each_mp(&block)
      @@defs.each(&block)
    end

    # Set the useOML flag. If set to false, make 'inject' a NOOP
    def self.__useOML__()
      @@useOML = true
    end

    # Returns the definition of this MP
    def self.__def__()
      unless (defs = @@defs[self])
	defs = @@defs[self] = {}
	defs[:p_def] = []
	defs[:seq_no] = 0
      end
      defs
    end

    # Set a name for this MP
    def self.name(name)
      __def__()[:name] = name
    end

    # Set the channel these measurements should be sent out on.
    # Multiple declarations are allowed, and ':default' identifies
    # the channel defined by the command line arguments or environment variables.
    #
    def self.channel(channel, domain = :default)
      (@@channels[self] ||= []) << [channel, domain]
    end

    # Set a metric for this MP
    # - name = name of the metric to set
    # - opts = a Hash with the options for this metric
    #          Only supported option is :type = { :string | :int32 | :double }
    def self.param(name, opts = {})
      o = opts.dup
      o[:name] = name
      o[:type] ||= :string
      if o[:type] == :long
	puts "OML4R: WARN: :long is deprecated use, :int32 instead"
	o[:type] = :int32
      end
      __def__()[:p_def] << o
      nil
    end

    # Inject a measurement from this Measurement Point to the OML Server
    # However, if useOML flag is false, then only prints the measurement on stdout
    # - args = a list of arguments (comma separated) which correspond to the
    #          different values of the metrics for the measurement to inject
    def self.inject(*args)
      return unless @@useOML

      # Check that the list of values passed as argument matches the
      # definition of this Measurement Point
      defs = __def__()
      pdef = defs[:p_def]
      if args.size != pdef.size
	raise "OML4R: Size mismatch between the measurement (#{args.size}) and the MP definition (#{pdef.size})!"
      end

      # Now prepare the measurement...
      t = Time.now - @@start_time
      a = []
      a << (defs[:seq_no] += 1)
      args.each do |arg|
	a << arg
      end
      # ...and inject it!
      msg = a.join("\t")
      @@channels[self].each do |ca|
	channel = ca[0]
	index = ca[1]
	channel.send "#{t}\t#{index}\t#{msg}"
      end
    end

    def self.start_time()
      @@start_time
    end

    # Freeze the definition of further MPs
    #
    def self.__freeze__(appName, start_time)
      return if @@frozen
      @@frozen = true
      # replace channel names with channel object
      self.each_mp do |klass, defs|
	cna = @@channels[klass] || []
	#puts "OML4R: '#{cna.inspect}', '#{klass}'"
	ca = cna.collect do |cname, domain|
	  # return it in an array as we need to add the channel specific index  
	  [Channel[cname.to_sym, domain.to_sym]]
	end
	#puts "Using channels '#{ca.inspect}"
	@@channels[klass] = ca.empty? ? [[Channel[]]] : ca
      end
      @@start_time = start_time

    end

    # Build the table schema for this MP and send it to the OML collection server
    # - name_prefix = the name for this MP to use as a prefix for its table
    def self.__print_meta__(name_prefix = nil)
      return unless @@frozen
      defs = __def__()

      # Do some sanity checks...
      unless (mp_name = defs[:name])
	raise "Missing 'name' declaration for '#{self}'"
      end
      unless (name_prefix.nil?)
	mp_name = "#{name_prefix}_#{mp_name}"
      end

      @@channels[self].each do |ca|
	#puts "Setting up channel '#{ca.inspect}"
	index = ca[0].send_schema(mp_name, defs[:p_def])
	ca << index
      end
    end
  end # class MPBase



  #
  # The Init method of OML4R
  # Ruby applications should call this method to initialise the OML4R module
  # This method will parse the command line argument of the calling application
  # to extract the OML specific parameters, it will also do the parsing for the
  # remaining application-specific parameters.
  # It will then connect to the OML server (if requested on the command line), and
  # send the initial instruction to setup the database and the tables for each MPs.
  #
  # - argv = the Array of command line arguments from the calling Ruby application
  # - & block = a block which defines the additional application-specific arguments
  #
  def self.init(argv, opts = {}, &block)

    if d = (ENV['OML_EXP_ID'] || opts[:expID])
      # XXX: It is still too early to complain about that. We need to be sure
      # of the nomenclature before making user-visible changes.
      #puts "OML4R: Depreciated - ENV['OML_EXP_ID'] || opts[:expID]"
      opts[:domain] ||= d
    end
    domain ||= ENV['OML_DOMAIN'] || opts[:domain]

    # XXX: Same as above; here, though, :id might actually be the way to go; or
    # perhaps instId?
    #if opts[:id]
    #  raise 'OML4R: :id is not a valid option. Do you mean :nodeID?'
    #end
    nodeID = ENV['OML_NAME'] || opts[:nodeID]  ||  opts[:id] || ENV['OML_ID'] 
    #
    # XXX: Same again; also, this is the responsibility of the developer, not the user
    #if opts[:app]
    #  raise 'OML4R: :app is not a valid option. Do you mean :appName?'
    #end
    appName = opts[:appName] || opts[:app]

    if  ENV['OML_URL'] || opts[:omlURL] || opts[:url]
      raise 'OML4R: neither OML_URL, :omlURL nor :url are valid. Do you mean OML_SERVER or :omlServer?'
    end
    omlCollectUri = ENV['OML_SERVER'] || opts[:omlServer]
    noop = opts[:noop] || false


    # Create a new Parser for the command line
    require 'optparse'
    op = OptionParser.new
    # Include the definition of application's specific arguments
    yield(op) if block
    # Include the definition of OML specific arguments
    op.on("--oml-file file", "Writes measurements to 'file'") { |name| omlCollectUri = "file:#{name}" }
    op.on("--oml-id id", "Name to identify this app instance [#{nodeID || 'undefined'}]") { |name| nodeID = name }
    op.on("--oml-exp-id expId", "Name to experiment DB [#{domain || 'undefined'}]") { |name| domain = name }
    op.on("--oml-domain expId", "Name to experiment DB [#{domain || 'undefined'}] *EXPERIMENTAL*") { |name| domain = name }
    op.on("--oml-server uri", "URI of server to send measurements to (tcp:host:port)") { |u|  omlCollectUri = u }
    op.on("--oml-noop", "Do not collect measurements") { noop = true }    
    op.on_tail("--oml-help", "Show this message") { puts op; exit }
    # XXX: This should be set by the application writer, not the command line
    #op.on("--oml-appid APPID", "Application ID for OML [#{appName || 'undefined'}] *EXPERIMENTAL*") { |name| appID = name }


    # Now parse the command line
    #puts "ARGV:>>> #{argv.inspect}"
    rest = op.parse(argv)
    return if noop


    Channel.create(:default, omlCollectUri) if omlCollectUri

    unless domain && nodeID && appName
      raise 'OML4R: Missing values for parameters :expID (--oml-exp-id), :nodeID (--oml-id), or :appName (in code)!'
    end

    # Handle the defined Measurement Points
    startTime = Time.now
    Channel.init_all(domain, nodeID, appName, startTime)
    msg = "OML4R enabled."
    Object.const_defined?(:MObject) ? MObject.debug(:oml4r, msg) : puts("OML4R: #{msg}")

    rest || []
  end

  #
  # Create a new channel over which to send measurement channels.
  #
  def self.create_channel(name, url)
    Channel.create(name.to_s, url)
  end

  #
  # Close the OML collection. This will block until all outstanding data have been sent out.
  # 
  def self.close()
    Channel.close_all
  end



  #
  # Measurement Point Class
  # Ruby applications using this module should sub-class this MPBase class
  # to define their own Measurement Point (see the example at the end of
  # this file)
  #
  class Channel
    @@channels = {}
    @@default_domain = nil

    def self.create(name, url, domain = :default)
      key = "#{name}:#{domain}"
      if channel = @@channels[key]
	if url != channel.url
	  raise "OML4R: Channel '#{name}' already defined with different url"
	end
	return channel
      end
      return self._create(key, domain, url)
    end

    def self._create(key, domain, url)
      #oml_opts = {:exp_id => 'image_load', :node_id => 'n1', :app_name => 'img_load'}
      if url.start_with? 'file:'
	proto, fname = url.split(':')
	out = (fname == '-' ? $stdout : File.open(fname, "w+"))
      elsif url.start_with? 'tcp:'
	#tcp:norbit.npc.nicta.com.au:3003
	proto, host, port = url.split(':')
	port ||= DEF_SERVER_PORT
	out = TCPSocket.new(host, port)
      else
	raise "OML4R: Unknown transport in server url '#{url}'"
      end
      #puts "Created channel for '#{key}'"
      @@channels[key] = self.new(url, domain, out)
    end

    def self.[](name = :default, domain = :default)
      key = "#{name}:#{domain}"
      unless (@@channels.key?(key))
	# If domain != :default and we have one for :default, create a new one
	if (domain != :default)
	  if (dc = @@channels["#{name}:default"])
	    return self._create(key, domain, dc.url)
	  end
	end
	raise "OML4R: Unknown channel '#{name}'"
      end
      @@channels[key]
    end

    def self.init_all(domain, nodeID, appName, startTime)
      @@default_domain = domain

      MPBase.__freeze__(appName, startTime)

      # send channel header
      @@channels.values.each { |c| c.init(nodeID, appName, startTime) }

      # send schema definitions
      MPBase.each_mp do |klass, defs|
	klass.__print_meta__(appName)
      end

      # add empty line to separate header form MP channel
      @@channels.values.each { |c| c.send "\n" }

      MPBase.__useOML__()
    end

    def self.close_all()
      @@channels.values.each { |c| c.close }
    end

    attr_reader :url

    def send_schema(mp_name, pdefs) # defs[:p_def]
      # Build the schema and send it
      @index += 1
      line = ['schema:', @index, mp_name]
      pdefs.each do |d|
	line << "#{d[:name]}:#{d[:type]}"
      end
      msg = line.join(' ')
      send msg
      @index
    end

    def send(msg)
      @queue.push msg
    end

    def init(nodeID, appName, startTime)
      send_protocol_header(nodeID, appName, startTime)
    end

    def close()
      @queue.push nil  # indicate end of work
      @runner.join()
    end

    protected
    def initialize(url, domain, out_channel)
      @domain = domain
      @url = url
      @out = out_channel
      @index = 0
      @queue = Queue.new
      start_runner
    end


    def send_protocol_header(nodeID, appName, startTime)
      @queue.push "protocol: 1"
      d = (@domain == :default) ? @@default_domain : @domain
      raise "Missing domain name" unless d
      @queue.push "experiment-id: #{d}"
      @queue.push "start_time: #{startTime.tv_sec}"
      @queue.push "sender-id: #{nodeID}"
      @queue.push "app-name: #{appName}"
      @queue.push "content: text"
    end

    def start_runner
      @runner = Thread.new do
	active = true
	begin
	  while (active)
	    msg = @queue.pop
	    active = !msg.nil?
	    if !@queue.empty?
	      ma = [msg]
	      while !@queue.empty?
		msg = @queue.pop
		if (active = !msg.nil?)
		  ma << msg
		end 
	      end
	      msg = ma.join("\n")
	    end
	    #puts ">>>>>>#{@domain}: <#{msg}>"
	    @out.puts msg unless msg.nil?
	    @out.flush
	  end
	  @out.close
	rescue Exception => ex
	  msg = "Exception while sending message to channel '#{@url}' (#{ex})"
	  Object.const_defined?(:MObject) ? MObject.warn(:oml4r, msg) : puts("OML4R: #{msg}")
	end
      end
    end

  end # Channel

end # module OML4R

# vim: sw=2