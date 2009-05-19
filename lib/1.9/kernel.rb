module Kernel
  alias_method :__callee__, :__method__
  
  def define_singleton_method(*args, &block)
    class << self
      self
    end.send(:define_method, *args, &block)
  end
  
  # No need to convert symbols to strings anymore
  def convert_to_constant_names(list)
    list
  end
end