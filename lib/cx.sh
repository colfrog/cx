cd() {
	builtin cd $@
	cxc -p "${PWD}"
}

cx() {
	cx_output="$(cxc -- $1)";
	if test -d "${cx_output}"; then
		builtin cd "${cx_output}"
	fi
}
