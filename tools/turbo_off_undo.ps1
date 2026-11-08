# Turns turbo boost back ON (boost mode = Enabled, Windows' default).
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PERFBOOSTMODE 1
powercfg /setdcvalueindex SCHEME_CURRENT SUB_PROCESSOR PERFBOOSTMODE 1
powercfg /setactive SCHEME_CURRENT
"Turbo boost back on."
