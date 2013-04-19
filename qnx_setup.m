function qnx_setup()

% Set up paths
curpath = pwd;
addpath(fullfile(curpath, 'qnx'));
addpath(fullfile(curpath, 'demos'));
addpath(fullfile(curpath, 'ext_mode'));
addpath(fullfile(curpath, 'ext_mode', 'target'));
savepath;

% Refresh customizations
sl_refresh_customizations;