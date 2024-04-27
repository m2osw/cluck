
Administrator Modified Files
============================

Please create files under `/etc/cluck/cluck.d` starting with two digits,
a dash and the name of the file found under the `/etc/cluck` directory.
Then add parameters that you want to overwrite to that file. For example,
the `/etc/cluck/cluck.conf` parameters can be overwritten using the
following file:

    /etc/cluck/cluck.d/50-cluck.conf

That way, you will continue to get the default configuration
changes from the source package under `/etc/cluck`.

All files get first loaded from `/etc/cluck` and then again
from `/etc/cluck/cluck.d`. Any parameter redefined in the
sub-directory overwrites the parameter of the same name in
the main directory.

The first two digits are used to sort the files. The first one
loaded is `00-<name>.conf` and the last one loaded is `99-<name>.conf`.
The administrator parameters are expected to be defined in a file using
number 50 as in `50-<name>.conf`. Other projects make changes using
filenames with lower numbers (i.e. `20-<name>.conf`) and number 80 is
considered special and used as the _global settings_. It gets copied to
all your machines using `snaprfs` and it overwrites your user settings.


Bugs
====

Submit bug reports and patches on
[github](https://github.com/m2osw/cluck/issues).


_This file is part of the [snapcpp project](https://snapwebsites.org/)._

