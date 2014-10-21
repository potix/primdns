#!/bin/sh
#

# Source function library.
. /etc/rc.d/init.d/functions

# global variables
prog=primd
retval=0

if [ -f /etc/sysconfig/${prog} ]; then
    . /etc/sysconfig/${prog}
fi

ulimit -n 65535

PID_FILE=${PID_FILE-"/var/run/${prog}.pid"}
EXEC_USER=${EXEC_USER-"root"}
EXEC_GROUP=${EXEC_USER-"root"}
EXEC_CMD=${EXEC_CMD-"/usr/sbin/${prog}"}
CONFIG_PATH=${CONFIG_PATH-"/etc/primd.conf"}
EXEC_CMD_ARGS=${EXEC_CMD_ARGS-"-M 20 -N 3"}

# util
running() {
        [ -f $1 ] || return 1
        PID=$(cat $1)
        ps -p ${PID} >/dev/null 2>/dev/null || return 1
        return 0
}

start() {
    local cmd="${EXEC_CMD} -c ${CONFIG_PATH} -P ${PID_FILE} ${EXEC_CMD_ARGS}"
    if [ -f ${PID_FILE} ] ; then
        if running ${PID_FILE} ; then
            echo "${prog} is already running."
            exit 1
        else
            rm -f ${PID_FILE}
        fi
    fi
    echo -n $"Starting ${prog}: "
    /bin/su - ${EXEC_USER} -s /bin/bash -c "/bin/bash -c '${cmd} > /dev/null 2>&1' &"
    retval=$?
    [ ${retval} -eq 0 ] && success
    echo
    return ${retval}
}

stop() {
    local pid=`cat ${PID_FILE} 2>/dev/null`
    echo -n $"Stopping ${prog}: "
    killproc -p ${PID_FILE}
    retval=$?
    echo
    rm -f ${PID_FILE}
    return ${retval}
}

reload() {
    local pid=`cat ${PID_FILE} 2>/dev/null`
    echo -n $"Reloading ${prog}: "
    kill -HUP ${pid}
    retval=$?
    echo
    return ${retval}
}

restart() {
    stop
    sleep 1
    start
}

status() {
    local pid=`cat ${PID_FILE}`
    if checkpid ${pid}; then
        echo "${prog} is running... (pid: ${pid})"
    else
        echo "${prog} is stopped"
    fi
}

case "$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
        restart
        ;;
    reload)
        reload
        ;;
    status)
        status
        ;;
    *)
        echo "Usage: ${prog} {start|stop|restart|reload|status}"
        exit 1
esac

exit ${retval}
