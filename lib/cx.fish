function __notify_cxd --on-variable PWD
	cxc -p "$PWD"
end

function cx
	set -l output (cxc "$argv")
	if test -d "$output"
		cd "$output"
	end
end
