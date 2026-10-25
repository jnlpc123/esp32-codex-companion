"""Build the read-only local UIA probe with the installed .NET compiler."""
from pathlib import Path
import os,subprocess
here=Path(__file__).resolve().parent
windows=Path(os.environ.get('WINDIR',r'C:\Windows'))
refs=[next((windows/'Microsoft.NET/assembly/GAC_MSIL'/name).glob('*/'+name+'.dll')) for name in ('UIAutomationClient','UIAutomationTypes')]
compiler=windows/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if not compiler.exists():compiler=windows/'Microsoft.NET/Framework/v4.0.30319/csc.exe'
subprocess.run([str(compiler),'/nologo','/target:exe','/codepage:65001','/out:'+str(here/'CodexApprovalProbe.exe'),*['/reference:'+str(p) for p in refs],'/reference:System.Web.Extensions.dll',str(here/'CodexApprovalProbe.cs')],check=True)
