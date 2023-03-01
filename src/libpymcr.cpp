#include "load_matlab.hpp"
#include "libpymcr.hpp"

namespace libpymcr {

    matlab::data::Array matlab_env::_conv_to_matlab(PyObject* input) {
        matlab::data::Array rv;
        if (PyCallable_Check(input)) {
            matlab::data::ArrayFactory factory;
            std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(input);
            std::uintptr_t conv = reinterpret_cast<std::uintptr_t>(&_converter);
            rv = factory.createStructArray({1, 1}, std::vector<std::string>({"func_ptr", "converter"}));
            rv[0][std::string("func_ptr")] = factory.createScalar<uint64_t>(addr);
            rv[0][std::string("converter")] = factory.createScalar<uint64_t>(conv);
        } else {
            rv = _converter.to_matlab(input);
        }
        return rv;
    }

    size_t matlab_env::_parse_inputs(std::vector<matlab::data::Array>& m_args,
                         py::args py_args,
                         py::kwargs& py_kwargs) {
        matlab::data::ArrayFactory factory;
        size_t nargout = 1;
        for (auto item: py_args) {
            m_args.push_back(_conv_to_matlab(item.ptr()));
        }
        for (auto item: py_kwargs) {
            std::string key(py::str(item.first));
            if (key.find("nargout") == 0) {
                nargout = item.second.cast<size_t>();
            } else {
                m_args.push_back(factory.createCharArray(std::string(py::str(item.first))));
                m_args.push_back(_conv_to_matlab(item.second.ptr()));
            }
        }
        return nargout;
    }
	
    py::object matlab_env::feval(const std::u16string &funcname, py::args args, py::kwargs& kwargs) {
        // Calls Matlab function
        const size_t nlhs = 0;
        // Clears the streams
        _m_output.get()->str(std::basic_string<char16_t>());
        _m_error.get()->str(std::basic_string<char16_t>());
        // Determines and converts the number of inputs
        std::vector<matlab::data::Array> outputs;
        std::vector<matlab::data::Array> m_args;
        size_t nargout = _parse_inputs(m_args, args, kwargs);
        // Release the GIL to call Matlab (PyBind automatically acquires GIL in all defined functions)
        py::gil_scoped_release gil_release;
        if (nargout == 1) {
            if (m_args.size() == 1) {
                outputs.push_back(_lib->feval(funcname, m_args[0], _m_output_buf, _m_error_buf));
            } else {
                outputs.push_back(_lib->feval(funcname, m_args, _m_output_buf, _m_error_buf));
            }
        } else {
            outputs = _lib->feval(funcname, nargout, m_args, _m_output_buf, _m_error_buf);
        }
        // Re-aquire the GIL
        py::gil_scoped_acquire gil_acquire;
        // Prints outputs and errors
        if(_m_output.get()->in_avail() > 0) {
            py::print(_m_output.get()->str(), py::arg("flush")=true); }
        if(_m_error.get()->in_avail() > 0) {
            py::print(_m_error.get()->str(), py::arg("file")=py::module::import("sys").attr("stderr"), py::arg("flush")=true); }
        // Converts outputs to Python types
        size_t n_out = outputs.size();
        py::object retval;
        if (n_out > 1) {
            retval = py::reinterpret_steal<py::object>(PyTuple_New(n_out));
            for (size_t idx = 0; idx < n_out; idx++) {
                if (outputs[idx].getNumberOfElements() == 0) {
                    Py_INCREF(Py_None);
                    PyTuple_SetItem(retval.ptr(), idx, Py_None);
                } else {
                    // Call the CPython function directly because we _want_ to steal a ref to the output
                    PyTuple_SetItem(retval.ptr(), idx, _converter.to_python(outputs[idx]));
                }
            }
        } else if (n_out == 1) {
            retval = py::reinterpret_steal<py::object>(_converter.to_python(outputs[0]));
        } else {
            retval = py::cast<py::none>(Py_None);
        }
        // Now clear temporary Matlab arrays created from Numpy array data inputs
        _converter.clear_py_cache();
        return retval;
    }

    py::object matlab_env::call(py::args args, py::kwargs& kwargs) {
        // Note this function only works with libpace.ctf
        return feval(u"call", args, kwargs);
    }

    // Constructor
    matlab_env::matlab_env(const std::u16string ctfname, std::string matlabroot) {
        _loadlibraries(matlabroot);
        auto mode = matlab::cpplib::MATLABApplicationMode::IN_PROCESS;
        // Specify MATLAB startup options
        std::vector<std::u16string> options = {u""};
        auto new_app = matlab::cpplib::initMATLABApplication(mode, options);
	int argc{0};
	const char * argv[] = {""};
	std::function<int(std::shared_ptr<matlab::cpplib::MATLABApplication>, const int, const char **)>
		main = [&](std::shared_ptr<matlab::cpplib::MATLABApplication> app, const int c, const char ** v){
	    _lib = matlab::cpplib::initMATLABLibrary(app, ctfname);
	    return 0;
	};
	matlab::cpplib::runMain(main, std::move(new_app), argc, argv);
        _converter = pymat_converter(pymat_converter::NumpyConversion::WRAP);
    }


} // namespace libpymcr


PYBIND11_MODULE(_libpymcr, m) {
    py::class_<libpymcr::matlab_env>(m, "matlab")
        .def(py::init<const std::u16string, std::string>(),
             py::arg("ctfname")=u"libpace.ctf", py::arg("matlabroot")="/usr/local/MATLAB/R2020a/")
        .def("feval", &libpymcr::matlab_env::feval)
        .def("call", &libpymcr::matlab_env::call);
}


// Run in Python with:
//>>> p = libpace.pace()
//>>> p.call('call', 'help')

