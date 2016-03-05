OPS-SWITCHD
===========

What is ops-switchd?
--------------------
ops-switchd is the OpenSwitch switching daemon that is a modified version of Open vSwitch. The ops-switchd daemon is responsible for driving the various switch configurations from the database into the hardware.

What is the structure of the repository?
----------------------------------------
* `vswitchd` - contains the source files for the ops-switchd daemon.
* `ovsdb` - contains the source files for the transactional database.
* `lib` - contains the library source files used by the ops-switchd daemon.
* `include` - contains the .h files.
* `ops/tests` - contains the automated tests for the ops-switchd daemon.
* `ops/docs` - contains the documents associated with the ops-switchd daemon.

What is the license?
--------------------
Apache 2.0 license. For more details refer to [COPYING](https://git.openswitch.net/cgit/openswitch/ops-openvswitch/tree/COPYING).

What other documents are available?
-----------------------------------
* For the high-level design of the ops-switchd daemon, see [DESIGN.md](http://www.openswitch.net/documents/dev/ops-switchd/DESIGN).
* For general information about OpenSwitch project, see http://www.openswitch.net.
