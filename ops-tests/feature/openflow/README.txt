### README ###
1) Make sure all the required utilities like tox, pip, flake8, docker etc are installed
2) Execute the test script using the below command
COMMAND: tox -e saenzpa -- --topology-platform docker -k test_ovs_openflow -s --topology-inject injection.json

NOTE:
1) We are using saenzpa repo of the Modular framework, because it has the support for eth0 interface.
2) Test case is developed on our openswitch image which is enabled with openflow, which is exported to the docker
3) Injection.json file is using the test script with our image
