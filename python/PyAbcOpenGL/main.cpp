//-*****************************************************************************
//
// Copyright (c) 2009-2012,
//  Sony Pictures Imageworks Inc. and
//  Industrial Light & Magic, a division of Lucasfilm Entertainment Company Ltd.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//-*****************************************************************************

#include <boost/python.hpp>
#if defined(PYALEMBIC_USE_STATIC_BOOST_PYTHON) && defined(PYILMBASE_STATICLIBS)
#include <PyIexAll.h>
#include <PyImathAll.h>
#endif

using namespace boost::python;

// forwards
void register_opengl();

BOOST_PYTHON_MODULE( alembicgl )
{
    docstring_options doc_options( true, true, false );
    
#if defined(PYALEMBIC_USE_STATIC_BOOST_PYTHON) && defined(PYILMBASE_STATICLIBS)
    object iexmodule(handle<>(borrowed(PyImport_AddModule("iex"))));
    if (PyErr_Occurred()) boost::python::throw_error_already_set();
    {
      scope iexscope(iexmodule);
      PyIex::register_all();
    }
    object imathmodule(handle<>(borrowed(PyImport_AddModule("imath"))));
    if (PyErr_Occurred()) boost::python::throw_error_already_set();
    {
      scope imathscope(imathmodule);
      PyImath::register_all();
    }
#else
    handle<> imath( PyImport_ImportModule( "imath" ) );
    if( PyErr_Occurred() ) throw_error_already_set();
#endif

    object package = scope();
    package.attr( "__path__" ) = "AbcOpenGL";
    register_opengl();

}
