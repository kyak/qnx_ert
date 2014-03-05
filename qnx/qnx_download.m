function qnx_download(modelName,makertwObj)

disp(['### Downloading ', modelName, ' to QNX Target Board...']);

if isunix
    disp('Download not implemented');
else
% Temporary file with commands for ftp
filename = [tempname,'.ftp'];
fid = fopen(filename, 'w');
[~, upname, ~] = fileparts(filename);
upname = [modelName,'_',upname];
if verLessThan('matlab', '8.1')
    bdir = makertwObj.BuildDirectory;
else
    bdir = rtwprivate('get_makertwsettings',gcs,'BuildDirectory');
end
ftpcmd = {
'verbose'
['open ',getpref('qnx_ert','TargetIP')]
'ftp'
'password'
'binary'
% Construct unique destination file name
['put ',fullfile(bdir,'..',modelName),' ',upname]
'bye'
};
for i=1:length(ftpcmd)
    fprintf(fid,'%s\n',ftpcmd{i});
end
fclose(fid);
command = sprintf('%s -s:%s','ftp',filename);
[status, out] = system(command);
disp(out);
delete(filename);

% Execute the uploaded file
if verLessThan('matlab', '8.1')
    tokens = makertwObj.BuildInfo.Tokens;
else
    tokens = makertwObj.Tokens;
end

for i=1:length(tokens)
    if strcmp(tokens(i).DisplayLabel,'|>QNX_MW_ROOT<|')
        QNX_MW_ROOT = tokens(i).Value;
    end
end

plink = fullfile(QNX_MW_ROOT,'qnx','plink.exe');

% Temporary file with commands for plink
filename = [tempname,'.plink'];
fid = fopen(filename, 'w');
plinkcmd = {
'root'
['chmod +x /tmp/',upname]
['/tmp/',upname,' &']
'exit'
};
for i=1:length(plinkcmd)
    fprintf(fid,'%s\n',plinkcmd{i});
end
fclose(fid);
command = sprintf('%s -telnet %s < %s',plink,getpref('qnx_ert','TargetIP'),filename);
[status, out] = system(command);
disp(out);
delete(filename);
end
