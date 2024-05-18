// Copyright (c) 2016-2024  Made to Order Software Corp.  All Rights Reserved
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#pragma once

// libexcept
//
#include    <libexcept/exception.h>



namespace cluck
{


DECLARE_LOGIC_ERROR(logic_error); // LCOV_EXCL_LINE
DECLARE_LOGIC_ERROR(unexpected_case);
DECLARE_OUT_OF_RANGE(out_of_range);

DECLARE_MAIN_EXCEPTION(cluck_exception);

DECLARE_EXCEPTION(cluck_exception, busy);
DECLARE_EXCEPTION(cluck_exception, invalid_message);
DECLARE_EXCEPTION(cluck_exception, invalid_parameter);
DECLARE_EXCEPTION(cluck_exception, timeout);


} // namespace cluck
// vim: ts=4 sw=4 et
