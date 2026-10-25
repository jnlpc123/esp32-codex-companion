// User-authorized, read-only UIA probe. Never invokes controls or reads chat text.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Automation;
using System.Runtime.InteropServices;

public class CodexApprovalProbe {
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    static readonly string[] Allow={"始终允许","允许此对话","允许一次","允许","批准","批准一次","批准此会话","是，继续","是，允许","Yes, proceed","Yes, allow","Allow once","Allow this session","Always allow","Approve","Approve once","Allow for this conversation"};
    static readonly string[] Deny={"拒绝","不允许","取消","不，取消","Deny","Decline","Cancel","No"};
    static Condition Names(string[] names) {
        var conditions=new List<Condition>();
        foreach(string name in names)conditions.Add(new PropertyCondition(AutomationElement.NameProperty,name));
        return new OrCondition(conditions.ToArray());
    }
    static string Hash(string title) {
        title=Regex.Replace(title.Trim(),@"\s+"," ");
        using(var sha=SHA256.Create()) {
            var result=new StringBuilder();foreach(byte b in sha.ComputeHash(Encoding.UTF8.GetBytes(title)))result.Append(b.ToString("x2"));return result.ToString();
        }
    }
    static bool Actionable(AutomationElement button) {
        var parent=button;
        for(int level=0;level<4;level++) {
            parent=TreeWalker.ControlViewWalker.GetParent(parent);if(parent==null)return false;
            var buttons=parent.FindAll(TreeScope.Descendants,new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.Button));
            if(buttons.Count>10)continue;
            var reject=parent.FindAll(TreeScope.Descendants,new AndCondition(new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.Button),new PropertyCondition(AutomationElement.IsEnabledProperty,true),Names(Deny)));
            if(reject.Count>0)return true;
        }
        return false;
    }
    static bool InDocument(AutomationElement button,AutomationElement document) {
        var parent=button;
        for(int i=0;i<40&&parent!=null;i++) {
            if(parent.Current.ControlType==ControlType.Document)return Automation.Compare(parent,document);
            parent=TreeWalker.ControlViewWalker.GetParent(parent);
        }
        return false;
    }
    static Dictionary<string,object> Sample() {
        var hashes=new HashSet<string>();var foreground=new HashSet<string>();bool available=false,sidebar=false;int groups=0,sidebarRows=0;
        var stopwatch=Stopwatch.StartNew();
        foreach(string executable in new string[]{"ChatGPT","Codex"})foreach(var process in Process.GetProcessesByName(executable)) {
            try {
                if(process.MainWindowHandle==IntPtr.Zero)continue;
                string path=process.MainModule.FileName;
                if(path.IndexOf("OpenAI.Codex_",StringComparison.OrdinalIgnoreCase)<0&&path.IndexOf(@"OpenAI\Codex\",StringComparison.OrdinalIgnoreCase)<0)continue;
                var root=AutomationElement.FromHandle(process.MainWindowHandle);if(root==null)continue;available=true;
                var document=root.FindFirst(TreeScope.Descendants,new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.Document));
                if(document!=null && GetForegroundWindow()==process.MainWindowHandle &&
                   !root.Current.IsOffscreen && !document.Current.IsOffscreen && !String.IsNullOrWhiteSpace(document.Current.Name))
                    foreground.Add(Hash(document.Current.Name));
                // Limit title/state lookup to the navigation region, never chat lists.
                Condition region=new OrCondition(new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.Pane),new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.Group),new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.Custom));
                var navigation=root.FindFirst(TreeScope.Descendants,new AndCondition(region,new PropertyCondition(AutomationElement.NameProperty,"首页")));
                if(navigation==null)navigation=root.FindFirst(TreeScope.Descendants,new AndCondition(region,new PropertyCondition(AutomationElement.NameProperty,"Home")));
                if(navigation!=null) {
                    sidebar=true;
                    var rows=navigation.FindAll(TreeScope.Descendants,new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.ListItem));
                    sidebarRows+=rows.Count;
                    foreach(AutomationElement row in rows) {
                        string name=row.Current.Name;
                        var match=Regex.Match(name,@"^(.*?)(?:等待批准|待批准|等待授权|需要批准|Waiting for approval|Needs approval)$",RegexOptions.IgnoreCase);
                        if(match.Success&&match.Groups[1].Value.Trim().Length>0)hashes.Add(Hash(match.Groups[1].Value));
                    }
                }
                var allows=root.FindAll(TreeScope.Descendants,new AndCondition(new PropertyCondition(AutomationElement.ControlTypeProperty,ControlType.Button),new PropertyCondition(AutomationElement.IsEnabledProperty,true),Names(Allow)));
                bool visible=false;
                foreach(AutomationElement button in allows) {
                    if(document==null||!InDocument(button,document))continue;
                    if(button.Current.IsOffscreen&&!root.Current.IsOffscreen)continue;
                    if(Actionable(button)){visible=true;break;}
                }
                if(visible) {
                    groups++;
                    // The app's top document name is the current chat title only.
                    if(document!=null&&!String.IsNullOrWhiteSpace(document.Current.Name))hashes.Add(Hash(document.Current.Name));
                }
            } catch(ElementNotAvailableException) {} catch(InvalidOperationException) {} catch(System.ComponentModel.Win32Exception) {}
            finally {process.Dispose();}
        }
        return new Dictionary<string,object>{{"available",available},{"sidebar_available",sidebar},{"sidebar_rows",sidebarRows},{"approval_title_hashes",new List<string>(hashes)},{"foreground_title_hashes",new List<string>(foreground)},{"visible_approval_groups",groups},{"query_ms",stopwatch.ElapsedMilliseconds}};
    }
    [MTAThread] public static void Main(string[] args) {
        Console.OutputEncoding=new UTF8Encoding(false);var json=new JavaScriptSerializer();bool once=Array.IndexOf(args,"--once")>=0;
        do {
            try {Console.WriteLine(json.Serialize(Sample()));}
            catch(Exception e) {Console.WriteLine(json.Serialize(new Dictionary<string,object>{{"available",false},{"error_type",e.GetType().Name}}));}
            if(!once)Thread.Sleep(350);
        } while(!once);
    }
}
