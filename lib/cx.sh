cd() {
	builtin cd $@
	ret=$?
	cxc -p "${PWD}"
	return $ret
}

cx() {
	cx_output="$(cxc $1)";
	if test -d "${cx_output}"; then
		builtin cd "${cx_output}"
	fi
}
