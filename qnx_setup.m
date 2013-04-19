function qnx_setup()

% Set up paths
curpath = pwd;
addpath(fullfile(curpath, 'qnx'));
addpath(fullfile(curpath, 'demos'));
addpath(fullfile(curpath, 'ext_mode'));
addpath(fullfile(curpath, 'ext_mode', 'target'));
savepath;

% Set up preferences
if ispref('qnx_ert')
	rmpref('qnx_ert');
end
% Ask for Target IP
tip = inputdlg('Enter Target Board IP address:');
addpref('qnx_ert','TargetIP',tip{1});

% Refresh customizations
sl_refresh_customizations;