using System;
using System.IO;
using System.Reflection;
using BDFramework.Configure;
using BDFramework.Core.Tools;
using UnityEngine;
#if ENABLE_HCLR
using HybridCLR;
#endif

namespace BDFramework
{
    /// <summary>
    /// 脚本加载器
    /// </summary>
    static public class ScriptLoder
    {
        private static readonly string Tag = "ScriptLoder";

        #region 路径

        /// <summary>
        /// 脚本目录
        /// </summary>
        static readonly public string SCRIPT_FOLDER_PATH = "script";

        //HCLR aot patch目录
        static readonly public string HCLR_AOT_PATCH_PATH = $"{SCRIPT_FOLDER_PATH}/hclr_aot_patch";

        /// <summary>
        /// 热更定义
        /// </summary>
        static readonly public string HOTFIX_DEFINE = "hotfix";

        /// <summary>
        /// 热更dll路径
        /// </summary>
        static readonly public string DLL_PATH = $"{SCRIPT_FOLDER_PATH}/{HOTFIX_DEFINE}.dll";

        /// <summary>
        /// dll pdb路径
        /// </summary>
        static readonly public string PDB_PATH = DLL_PATH + ".pdb";

        #endregion


        #region 加密

        /// <summary>
        /// 私钥
        /// </summary>
        static public string PrivateKey { get; set; } = null;

        /// <summary>
        /// 公钥
        /// </summary>
        static public string PublicKey { get; set; } = null;

        #endregion

        /// <summary>
        /// 反射注册
        /// </summary>
        private static Action<bool> CLRBindAction { get; set; }

        /// <summary>
        /// 脚本加载入口
        /// </summary>
        /// <param name="loadPathType"></param>
        /// <param name="runMode"></param>
        /// <param name="mainProjectTypes">UPM隔离了dll,需要手动传入</param>
        static public void Init(AssetLoadPathType loadPathType, HotfixCodeRunMode runMode, Type[] mainProjectTypes)
        {
            BDebug.EnableLog(Tag);
            // CLRBindAction = clrBindingAction;

            if (loadPathType == AssetLoadPathType.Editor)
            {
                BDebug.Log(Tag, "Editor(非热更)模式!");
                //反射调用，防止编译报错
                var assembly = Assembly.GetExecutingAssembly();
                var type = assembly.GetType("BDLauncherBridge");
                var method = type.GetMethod("Start", BindingFlags.Public | BindingFlags.Static);
                //添加框架部分的type，热更下不需要，打包会把框架的部分打进去
                // var list = new List<Type>();
                // list.AddRange(mainProjectTypes);
                // list.AddRange(typeof(BDLauncher).Assembly.GetTypes());
                method.Invoke(null, new object[] {mainProjectTypes, null});
            }
            else
            {
                BDebug.Log(Tag, "热更模式!");
                LoadHotfixDLL(loadPathType, runMode, mainProjectTypes);
            }
        }


        /// <summary>
        /// 加载
        /// </summary>
        /// <param name="source"></param>
        /// <param name="copyto"></param>
        /// <returns></returns>
        static public void LoadHotfixDLL(AssetLoadPathType loadPathType, HotfixCodeRunMode mode, Type[] mainProjecTypes)
        {
            //路径
            var dllPath = Path.Combine(GameBaseConfigProcessor.GetLoadPath(loadPathType), BApplication.GetRuntimePlatformPath(), DLL_PATH);
            //反射执行
            if (mode == HotfixCodeRunMode.HyCLR)
            {
                if (!Application.isEditor)
                {
                    BDebug.Log("【ScriptLoder】HCLR执行, Dll路径:" + dllPath, Color.red);
                    //加载AOT,AOT Pacth 一定在母包内
                    var aotPatch = Path.Combine(BApplication.GetRuntimePlatformPath(), HCLR_AOT_PATCH_PATH);
                    var aotPatchDlls = BetterStreamingAssets.GetFiles(aotPatch, "*.dll");
                    foreach (var path in aotPatchDlls)
                    {
                        BDebug.Log("【ScriptLoder】HCLR加载AOT Patch:" + path, Color.red);
                        var dllbytes = BetterStreamingAssets.ReadAllBytes(path);
                        #if ENABLE_HCLR
                        var err = RuntimeApi.LoadMetadataForAOTAssembly(dllbytes, HomologousImageMode.SuperSet);
                        Debug.Log($"LoadMetadataForAOTAssembly:{path}. ret:{err}");
                        #endif
                    }
                }
                else
                {
                    BDebug.Log("【ScriptLoder】Editor下反射执行, Dll路径:" + dllPath, Color.red);
                }

                //HyCLR加载
                Assembly assembly;
                var dllBytes = File.ReadAllBytes(dllPath);
                var pdbPath = dllPath + ".pdb";
                if (File.Exists(pdbPath))
                {
                    BDebug.Log("【ScriptLoder】加载pdb:" + pdbPath, Color.yellow);
                    var pdbBytes = File.ReadAllBytes(pdbPath);
                    assembly = Assembly.Load(dllBytes, pdbBytes);
                }
                else
                {
                    BDebug.Log("【ScriptLoder】HCLR执行, Dll路径:" + dllPath, Color.red);
                    assembly = Assembly.Load(dllBytes);
                }

                var type = typeof(ScriptLoder).Assembly.GetType("BDLauncherBridge");
                var method = type.GetMethod("Start", BindingFlags.Public | BindingFlags.Static);
                var startFunc = (Action<Type[], Type[]>) Delegate.CreateDelegate(typeof(Action<Type[], Type[]>), method);
                try
                {
                    var hotfixTypes = assembly.GetTypes();
                    //开始
                    startFunc(mainProjecTypes, hotfixTypes);
                }
                catch (ReflectionTypeLoadException e)
                {
                    foreach (var exc in e.LoaderExceptions)
                    {
                        Debug.LogError(exc);
                    }
                }
            }
            //解释执行
            // else if (mode == HotfixCodeRunMode.ILRuntime)
            // {
            //     BDebug.Log("【ScriptLoder】热更Dll路径:" + dllPath, Color.red);
            //     //解释执行模式
            //     ILRuntimeHelper.LoadHotfix(dllPath, CLRBindAction);
            //     var hotfixTypes = ILRuntimeHelper.GetHotfixTypes().ToArray();
            //     ILRuntimeHelper.AppDomain.Invoke("BDLauncherBridge", "Start", null, new object[] { mainProjecTypes, hotfixTypes });
            // }
            else
            {
                BDebug.Log("【ScriptLoder】Dll路径:内置", Color.magenta);
            }
        }


        /// <summary>
        /// 获取当前本地DLL
        /// </summary>
        static public string GetLocalDLLPath(string root, RuntimePlatform platform)
        {
            return IPath.Combine(root, BApplication.GetPlatformPath(platform), DLL_PATH);
        }
    }
}
