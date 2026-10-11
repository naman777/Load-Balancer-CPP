/**
 * Sets the theme class before first paint. A server component on purpose:
 * the ThemeScript exported from theme-provider is a client component, and
 * React warns about script tags rendered on the client. Keep the storage key
 * in step with ThemeProvider's.
 */
export function ThemeScript() {
  const code = `(function(){try{var t=localStorage.getItem('chaiui-theme');if(t!=='light'&&t!=='dark')t='dark';document.documentElement.classList.toggle('dark',t==='dark');document.documentElement.style.colorScheme=t}catch(e){}})()`
  return <script dangerouslySetInnerHTML={{ __html: code }} />
}
