class BreakpointTracker
  
  class Handler
    def initialize(meth, ip, line, prc)
      @method = meth
      @ip = ip
      @line = line
      @handler = prc
    end
    
    def install
      @orig = @method.bytecodes.get_byte(@ip)
      @method.bytecodes.set_byte(@ip, Rubinius::DEBUG_INST)
    end
    
    def restore_into(ctx)
      @method.bytecodes.set_byte(@ip, @orig)
      ctx.ip = ctx.ip - 1
    end
    
    def call(ctx)
      @handler.call(ctx)
    end
  end
  
  def initialize
    @handlers = Hash.new { |h,k| h[k] = {} }
    @debug_channel = Channel.new
    @control_channel = Channel.new
  end
  
  def add_thread(thr)
    thr.set_debugging @debug_channel, @control_channel
  end
  
  def on(method, line, &prc)
    cm = method.compiled_method
    tip = cm.first_ip_on_line(line)
    
    handler = Handler.new(cm, tip, line, prc)
    @handlers[cm][tip] = handler
    handler.install
    return handler
  end
  
  def find_handler(ctx)
    cm = ctx.method
    ip = ctx.ip - 1
    @handlers[cm][ip]
  end
  
  def process
    ctx = @debug_channel.receive
    pnt = find_handler(ctx)
    unless pnt
      raise "Unable to find handler for #{ctx.inspect}"
    end
    pnt.call(ctx)
    pnt.restore_into(ctx)
    @control_channel.send nil
  end
end