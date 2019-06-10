class MatlabFunction(object):

    def __init__(self, interface, fun, converter=None, parent=None, caller=None):
        '''

        Create a proxt function to handle matlab calls

        :param interface: The callable MATLAB interface (where we run functions)
        :param parent: What the function has been called from. Needed for handle class updates.
        :param fun: String name of function to be executed.
        '''
        self._interface = interface
        self.converter = converter
        if parent is None:
            self._parent = []
        else:
            self._parent = parent
        self._fun = fun
        self._caller = caller

    def __call__(self, *args, nargout=None, **kwargs):
        """Call the Matlab function.

        Calling this function will transfer all function arguments
        from Python to Matlab, and translate them to the appropriate
        Matlab data structures.

        Return values are translated the same way, and transferred
        back to Python.

        Parameters
        ----------
        nargout : int
            Call the function in Matlab with this many output
            arguments. If the argument not given, we will try and work
            out the correct value.
        **kwargs : dict
            Keyword arguments are transparently translated to Matlab's
            key-value pairs. For example, ``matlab.struct(foo="bar")``
            will be translated to ``struct('foo', 'bar')``.

        """
        # serialize keyword arguments:
        args += sum(kwargs.items(), ())
        args = self.converter.encode(args)

        # Determination of the number of output arguments is a pain.
        if nargout is None:
            nargout = int(self._interface.getArgOut(self._fun, nargout=1))

        if not args:
            args = []
        if nargout > 0:
            d = self._interface.call2(self._fun, self._parent, args, nargout=nargout)
        else:
            self._interface.call2(self._fun, self._parent, args, nargout=nargout)
            if self._caller is not None:
                self._caller.updateProxy()
            return

        return self.converter.decode(d)
