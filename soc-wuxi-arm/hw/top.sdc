## Minimal timing constraints for the SoC coursework Quartus project.
##
## Board/project assumptions:
##   - CLK is the DE10-Lite 50 MHz oscillator input.
##   - TCK is an external debug/JTAG/SWD clock and is asynchronous to CLK.
##   - RESETn is an asynchronous board-level reset input.

set_time_format -unit ns -decimal_places 3

# 50 MHz system clock.
create_clock -name {CLK} -period 20.000 -waveform {0.000 10.000} [get_ports {CLK}]

# External debug clock. The exact debugger rate can vary; 10 MHz is a safe
# initial constraint for TimeQuest and can be tightened later if needed.
create_clock -name {TCK} -period 100.000 -waveform {0.000 50.000} [get_ports {TCK}]

# CLK and TCK belong to independent clock domains.
set_clock_groups -asynchronous \
    -group [get_clocks {CLK}] \
    -group [get_clocks {TCK}]

# RESETn is an asynchronous external reset. Its release should be synchronized
# in a production design; for this coursework project we exclude it from normal
# data-path timing analysis to avoid misleading recovery/removal violations.
set_false_path -from [get_ports {RESETn}]

# Let Quartus add device/model-specific clock uncertainty.
derive_clock_uncertainty
