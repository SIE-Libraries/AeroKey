#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "aero_key.h"

namespace py = pybind11;
using namespace aerokey;

PYBIND11_MODULE(_aerokey, m) {
    m.doc() = "pybind11 bindings for AeroKey C++ core";

    py::enum_<AeroType>(m, "AeroType")
        .value("Redis", AeroType::Redis)
        .value("MongoDB", AeroType::MongoDB)
        .value("FireStore", AeroType::FireStore)
        .value("AWSDynamoDB", AeroType::AWSDynamoDB)
        .export_values();

    py::enum_<AeroStatus>(m, "AeroStatus")
        .value("Ok", AeroStatus::Ok)
        .value("NotFound", AeroStatus::NotFound)
        .value("InvalidArgument", AeroStatus::InvalidArgument)
        .value("BackendUnavailable", AeroStatus::BackendUnavailable)
        .value("CallbackError", AeroStatus::CallbackError)
        .export_values();

    py::class_<AeroUrl>(m, "AeroUrl")
        .def(py::init<std::string, std::string>(), py::arg("url"), py::arg("key"))
        .def_readwrite("endpoint", &AeroUrl::endpoint)
        .def_readwrite("key", &AeroUrl::key);

    py::class_<AeroError>(m, "AeroError")
        .def(py::init<>())
        .def_readwrite("code", &AeroError::code)
        .def_readwrite("message", &AeroError::message)
        .def("ok", &AeroError::ok);

    py::class_<AeroWriteResult>(m, "AeroWriteResult")
        .def(py::init<>())
        .def_readwrite("written", &AeroWriteResult::written)
        .def_readwrite("error", &AeroWriteResult::error);

    py::class_<AeroResponse>(m, "AeroResponse")
        .def(py::init<>())
        .def_readwrite("backend", &AeroResponse::backend)
        .def_readwrite("records", &AeroResponse::records)
        .def_readwrite("error", &AeroResponse::error);

    m.def("AeroPush", &AeroPush, py::arg("type"), py::arg("url"), py::arg("payload"));
    m.def("AeroFetch", &AeroFetch, py::arg("type"), py::arg("url"));
    m.def("AeroStream", &AeroStream, py::arg("url"), py::arg("type"), py::arg("func"));
    m.def("ResetProviders", &ResetProviders);
}
