#!/usr/bin/env bash
# Kills any running echo_server and load_balancer processes.
pkill -f echo_server   2>/dev/null && echo "Stopped echo_server(s)"   || echo "echo_server not running"
pkill -f load_balancer 2>/dev/null && echo "Stopped load_balancer"    || echo "load_balancer not running"
