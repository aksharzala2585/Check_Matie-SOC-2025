#include <bits/stdc++.h>
using namespace std;
#define rp(j, l, n) for(decltype(n) j = l; (l < n) ? j < n : j > n; (l < n) ? j++ : j--)
#define ll long long
#define vi vector<ll>
#define vvi vector<vi>
#define inp(v, n) rp(i, 0, n)cin>>v[i];


ll dif (vvi &sol, vi &a, ll i, ll j)
{
    if(sol[i][j]!=-1e9 - 1)
    {
        return sol[i][j];
    }
    ll ans =0;
    if(j==i)
    {
        ans = a[i];
    }
    else if(j-i==1)
    {
        ans = max(a[i],a[j]) - min(a[i],a[j]);
        
    }
    else
    {
        ans = max((a[i] - dif(sol, a,i+1, j)), (a[j] - dif(sol, a, i , j-1)));
    }
    sol[i][j] = ans;
    // cout<< i<< " " <<j<<endl;
    return ans;
}
void solve()
{
    ll n;
    cin>>n;
    vi a(n);
    inp(a,n);
    vvi sol(n, vi(n, -1e9 -1));
    if(dif(sol, a,0,n-1)>0)
    {
        cout<< "Player 1 wins"<<endl;
    }
    else if(dif(sol, a,0,n-1)<0)
    {
        cout<< "Player 2 wins"<<endl;
    }
    else
    {
        cout<<"Its a draw"<<endl;
    }
}
int main()
{
    ios::sync_with_stdio(0); cin.tie(0);
    solve();
    return 0;
}