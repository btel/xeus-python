/***************************************************************************
* Copyright (c) 2018, Martin Renou, Johan Mabille, Sylvain Corlay, and     *
* Wolf Vollprecht                                                          *
* Copyright (c) 2018, QuantStack                                           *
*                                                                          *
* Distributed under the terms of the BSD 3-Clause License.                 *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
****************************************************************************/

#include <string>
#include <utility>

#include "nlohmann/json.hpp"

#include "xeus/xcomm.hpp"
#include "xeus/xinterpreter.hpp"

#include "pybind11_json/pybind11_json.hpp"

#include "pybind11/pybind11.h"
#include "pybind11/functional.h"

#include "xcomm.hpp"
#include "xutils.hpp"
#include "xinteractiveshell.hpp"

namespace py = pybind11;
namespace nl = nlohmann;

namespace xpyt
{
    /*********************
     * xcomm declaration *
     ********************/

    class xcomm
    {
    public:

        using python_callback_type = std::function<void(py::object)>;
        using cpp_callback_type = std::function<void(const xeus::xmessage&)>;
        using zmq_buffers_type = std::vector<zmq::message_t>;

        xcomm(const py::args& args, const py::kwargs& kwargs);
        xcomm(xeus::xcomm&& comm);
        xcomm(xcomm&& comm) = default;
        virtual ~xcomm();

        std::string comm_id() const;
        bool kernel() const;

        void close(const py::args& args, const py::kwargs& kwargs);
        void send(const py::args& args, const py::kwargs& kwargs);
        void on_msg(const python_callback_type& callback);
        void on_close(const python_callback_type& callback);

    private:

        xeus::xtarget* target(const py::kwargs& kwargs) const;
        xeus::xguid id(const py::kwargs& kwargs) const;
        cpp_callback_type cpp_callback(const python_callback_type& callback) const;

        xeus::xcomm m_comm;
    };

    /************************
     * xcomm implementation *
     ************************/

    xcomm::xcomm(const py::args& /*args*/, const py::kwargs& kwargs)
        : m_comm(target(kwargs), id(kwargs))
    {
        m_comm.open(
            kwargs.attr("get")("metadata", py::dict()),
            kwargs.attr("get")("data", py::dict()),
            pylist_to_zmq_buffers(kwargs.attr("get")("buffers", py::list()))
        );
    }

    xcomm::xcomm(xeus::xcomm&& comm)
        : m_comm(std::move(comm))
    {
    }

    xcomm::~xcomm()
    {
    }

    std::string xcomm::comm_id() const
    {
        return m_comm.id();
    }

    bool xcomm::kernel() const
    {
        return true;
    }

    void xcomm::close(const py::args& /*args*/, const py::kwargs& kwargs)
    {
        m_comm.close(
            kwargs.attr("get")("metadata", py::dict()),
            kwargs.attr("get")("data", py::dict()),
            pylist_to_zmq_buffers(kwargs.attr("get")("buffers", py::list()))
        );
    }

    void xcomm::send(const py::args& /*args*/, const py::kwargs& kwargs)
    {
        m_comm.send(
            kwargs.attr("get")("metadata", py::dict()),
            kwargs.attr("get")("data", py::dict()),
            pylist_to_zmq_buffers(kwargs.attr("get")("buffers", py::list()))
        );
    }

    void xcomm::on_msg(const python_callback_type& callback)
    {
        m_comm.on_message(cpp_callback(callback));
    }

    void xcomm::on_close(const python_callback_type& callback)
    {
        m_comm.on_close(cpp_callback(callback));
    }

    xeus::xtarget* xcomm::target(const py::kwargs& kwargs) const
    {
        std::string target_name = kwargs["target_name"].cast<std::string>();
        return xeus::get_interpreter().comm_manager().target(target_name);
    }

    xeus::xguid xcomm::id(const py::kwargs& kwargs) const
    {
        if (py::hasattr(kwargs, "comm_id"))
        {
            // TODO: prevent copy
            return xeus::xguid(kwargs["comm_id"].cast<std::string>());
        }
        else
        {
            return xeus::new_xguid();
        }
    }

    auto xcomm::cpp_callback(const python_callback_type& py_callback) const -> cpp_callback_type
    {
        return [this, py_callback](const xeus::xmessage& msg) {
            XPYT_HOLDING_GIL(py_callback(cppmessage_to_pymessage(msg)))
        };
    }

    void register_target(const py::str& target_name, const py::object& callback)
    {
        auto target_callback = [&callback] (xeus::xcomm&& comm, const xeus::xmessage& msg) {
            XPYT_HOLDING_GIL(callback(xcomm(std::move(comm)), cppmessage_to_pymessage(msg)));
        };

        xeus::get_interpreter().comm_manager().register_comm_target(
            static_cast<std::string>(target_name), target_callback
        );
    }

    namespace detail
    {
        struct xmock_object
        {
        };
    }

    struct xmock_kernel
    {
        xmock_kernel() {}

        inline py::object parent_header() const
        {
            return py::dict(py::arg("header")=xeus::get_interpreter().parent_header().get<py::object>());
        }
    };

    /*****************
     * kernel module *
     *****************/

    py::module get_kernel_module_impl()
    {
        py::module kernel_module = py::module("kernel");

        py::class_<detail::xmock_object> _Mock(kernel_module, "_Mock");
        py::class_<xinteractive_shell> XInteractiveShell(
            kernel_module, "XInteractiveShell", py::dynamic_attr());

        py::class_<hooks_object>(kernel_module, "Hooks")
            .def_static("show_in_pager", &hooks_object::show_in_pager);
        py::class_<xeus::xhistory_manager>(kernel_module, "HistoryManager");

        XInteractiveShell.def(py::init<>())
            .def_property_readonly("magics_manager", &xinteractive_shell::get_magics_manager) .def_property_readonly("extension_manager", &xinteractive_shell::get_extension_manager)
            .def_property_readonly("hooks", &xinteractive_shell::get_hooks)
            .def_property_readonly("db", &xinteractive_shell::get_db)
            .def_property_readonly("user_ns", &xinteractive_shell::get_user_ns)
            .def_property_readonly("builtin_trap", &xinteractive_shell::get_builtin_trap)
            .def_property_readonly("ipython_dir", &xinteractive_shell::get_ipython_dir)
            .def_property_readonly("dir_stack", &xinteractive_shell::get_dir_stack)
            .def_property_readonly("home_dir", &xinteractive_shell::get_home_dir)
            .def_property_readonly("history_manager", [](xinteractive_shell &a){return &a.get_history_manager();})
            .def("run_line_magic", &xinteractive_shell::run_line_magic)
            .def("run_cell_magic", &xinteractive_shell::run_cell_magic)
            // magic is deprecated but some magic functions still use it
            .def("magic", &xinteractive_shell::run_line_magic,
                "name"_a,
                "arg"_a="")
            .def("system", &xinteractive_shell::system)
            .def("getoutput", &xinteractive_shell::getoutput)
            .def("register_post_execute", &xinteractive_shell::register_post_execute)
            .def("enable_gui", &xinteractive_shell::enable_gui)
            .def("showtraceback", &xinteractive_shell::showtraceback)
            .def("observe", &xinteractive_shell::observe)
            .def("register_magic_function",
                &xinteractive_shell::register_magic_function,
                "Register magic function",
                py::arg("func"),
                py::arg("magic_kind")="line",
                py::arg("magic_name")=py::none())
            .def("register_magics", &xinteractive_shell::register_magics);

        py::module::import("IPython.core.interactiveshell").attr("InteractiveShellABC").attr("register")(XInteractiveShell);

        py::class_<xcomm>(kernel_module, "Comm")
            .def(py::init<py::args, py::kwargs>())
            .def("close", &xcomm::close)
            .def("send", &xcomm::send)
            .def("on_msg", &xcomm::on_msg)
            .def("on_close", &xcomm::on_close)
            .def_property_readonly("comm_id", &xcomm::comm_id)
            .def_property_readonly("kernel", &xcomm::kernel);
        py::class_<xmock_kernel>(kernel_module, "mock_kernel", py::dynamic_attr())
            .def(py::init<>())
            .def_property_readonly("_parent_header", &xmock_kernel::parent_header);

        kernel_module.def("register_target", &register_target);

        py::object kernel = kernel_module.attr("mock_kernel")();
        py::object comm_manager = kernel_module.attr("_Mock");
        comm_manager.attr("register_target") = kernel_module.attr("register_target");
        kernel.attr("comm_manager") = comm_manager;

        py::object xeus_python =  kernel_module.attr("XInteractiveShell")();
        xeus_python.attr("kernel") = kernel;

        kernel_module.def("get_ipython", [xeus_python]() {
            return xeus_python;
        });

        return kernel_module;
    }

    py::module get_kernel_module()
    {
        static py::module kernel_module = get_kernel_module_impl();
        return kernel_module;
    }
}
