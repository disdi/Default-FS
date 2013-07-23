DIRS := $(filter-out LDDK README SEQUENCE VERSIONS Makefile Makefile.mixed module.mk $(wildcard *.sh), $(wildcard *))
DIRS_CLEAN := $(DIRS:=_clean)
WORKSHOP_CONTENT := $(filter-out Project $(wildcard *.sh), $(wildcard *))
PROJECTSHOP_CONTENT := LDDK README VERSIONS Makefile Project
TEMPLATES_FILE := templates.tgz

KERNEL_SOURCE := /usr/src/linux

.PHONY: all ${DIRS} clean

all: ${DIRS}

${DIRS}:
	@echo -n "Building $@ ... "
	@${MAKE} -C $@ KERNEL_SOURCE=${KERNEL_SOURCE} > /dev/null
	@echo "done"

allclean: clean
	@${RM} *.sh

clean: ${DIRS_CLEAN}

%_clean:
	@echo -n "Cleaning $* ... "
	@${MAKE} -C $* KERNEL_SOURCE=${KERNEL_SOURCE} clean > /dev/null
	@echo "done"

workshop:
	@${MAKE} clean
	tar -zcvf ${TEMPLATES_FILE} ${WORKSHOP_CONTENT} > /dev/null
	../create_package ${TEMPLATES_FILE} lddk_package.sh
	${RM} ${TEMPLATES_FILE}

projectshop:
	@${MAKE} clean
	tar -zchvf ${TEMPLATES_FILE} ${PROJECTSHOP_CONTENT} > /dev/null
	../create_package ${TEMPLATES_FILE} lddk_project.sh
	${RM} ${TEMPLATES_FILE}
