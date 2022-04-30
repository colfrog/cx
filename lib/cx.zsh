function chpwd() {
	emulate -L zsh
	cxc -p "${PWD}"
}

function cx() {
	local output="`cxc \"$1\"`"
	if test -d "${output}"; then
		cd "${output}"
	fi
}
