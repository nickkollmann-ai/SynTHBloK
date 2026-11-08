# Puts the CPU back to 100% (turbo on) in the current Windows power plan.
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX 100
powercfg /setdcvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX 100
powercfg /setactive SCHEME_CURRENT
"CPU back to 100% (turbo on)."
