import paramiko

ssh_client = paramiko.SSHClient()
ssh_client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

import sys
import time
import logging
import re
from watchdog.observers import Observer
from watchdog.events import LoggingEventHandler
from watchdog.events import FileSystemEventHandler

remoteBasePath = "/root/api/"
remoteAddress  = "192.168.12.1"
remoteUserName = "root"
remotePassword = "chronos"

ignoreFiles = re.compile(".*#.*~?")

fileTranslatePattern = {'.\\':'', '\\':'/', ' ':'\ ', '(':'\(', ')':'\)'}
fileTranslatePattern = dict((re.escape(k),v) for k, v in fileTranslatePattern.items())
pattern = re.compile('|'.join(fileTranslatePattern.keys()))

def translatePath(path):
    return pattern.sub(lambda m: fileTranslatePattern[re.escape(m.group(0))], path)

class syncFilesEventHandler (FileSystemEventHandler):
    def included(self, event):
        if ignoreFiles.match(event.src_path):
            return False
        return True


    def copyToRemote(self, scp_client, path, is_directory=False):
        try:
            scp_client.chdir(remoteBasePath)
            if (is_directory):
                scp_client.mkdir(translatePath(path))
            else:
                logging.info('origPath: %s', path)
                logging.info('path: %s', translatePath(path))
                scp_client.put(path, translatePath(path))
        except PermissionError:
            print ("Permission Denied on \"%s\"" % (remoteBasePath + translatePath(path)))
        
    
    def on_created(self, event):
        if self.included(event):
            print ("created: ", event, end='')
            ssh_client.connect(hostname=remoteAddress, username=remoteUserName, password=remotePassword)
            scp_client = ssh_client.open_sftp()
            self.copyToRemote(scp_client, event.src_path, is_directory=event.is_directory)
            scp_client.close()
            ssh_client.close()
            print (" sync'd")
        
    def on_deleted(self, event):
        if self.included(event):
            print ("deleted: ", event, end='')
            ssh_client.connect(hostname=remoteAddress, username=remoteUserName, password=remotePassword)
            scp_client = ssh_client.open_sftp()

            try:
                scp_client.chdir(remoteBasePath)
                if (event.is_directory):
                    scp_client.rmdir(translatePath(event.src_path))
                else:
                    scp_client.remove(translatePath(event.src_path))
            except FileNotFoundError:
                print("no such file/dir")
            except  IOError:
                print("IO Error erasing file/dir")

            scp_client.close()
            ssh_client.close()
                
            print (" sync'd")

    def on_moved(self, event):
        if self.included(event):
            print ("moved: ", event, end='')

            ssh_client.connect(hostname=remoteAddress, username=remoteUserName, password=remotePassword)
            scp_client = ssh_client.open_sftp()
            try:
                scp_client.chdir(remoteBasePath)
                scp_client.posix_rename(translatePath(event.src_path), translatePath(event.dest_path))
            except IOError:
                print("--- Error while moving")

            scp_client.close()
            ssh_client.close()
            
            print (" sync'd")
            
    def on_modified(self, event):
        if self.included(event):
            print ("modified: ", event, end='')
            if event.is_directory:
                logging.info("directory, ignoring")
            else:
                ssh_client.connect(hostname=remoteAddress, username=remoteUserName, password=remotePassword)
                scp_client = ssh_client.open_sftp()
                scp_client.chdir(remoteBasePath)
                logging.info('origPath: %s', event.src_path)
                logging.info('path: %s', translatePath(event.src_path))
                scp_client.put(event.src_path, translatePath(event.src_path))
                scp_client.close()
                ssh_client.close()
                print (" sync'd")




if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO,
                        format='%(asctime)s - %(message)s',
                        datefmt='%Y-%m-%d %H:%M:%S')
    path = sys.argv[1] if len(sys.argv) > 1 else '.'
    #event_handler = LoggingEventHandler()
    event_handler = syncFilesEventHandler()
    observer = Observer()
    observer.schedule(event_handler, path, recursive=True)
    observer.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
    observer.join()
