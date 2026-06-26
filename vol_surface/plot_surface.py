# plot_surface.py
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from scipy.interpolate import griddata
from mpl_toolkits.mplot3d import Axes3D

df = pd.read_csv("iv_surface.csv")

# Split calls and puts — plot calls, they're cleaner
calls = df[df['type'] == 'call'].copy()

# Instead of hardcoded IV bounds
calls = calls[
    (calls['iv'] > calls['iv'].quantile(0.05)) &  # bottom 5% is noise
    (calls['iv'] < calls['iv'].quantile(0.95))     # top 5% is noise
]

# Instead of hardcoded strike range
moneyness = calls['strike'] / calls['spot']  # log-moneyness is even better
calls = calls[
    (moneyness >= 0.85) &
    (moneyness <= 1.15)
]

calls['spread_pct'] = (calls['ask'] - calls['bid']) / calls['mid']
calls = calls[calls['spread_pct'] < 0.25]  # spread < 25% of mid price

# Interpolate onto a regular grid for smooth surface
xi = np.linspace(calls.strike.min(), calls.strike.max(), 60)
yi = np.linspace(calls.days_to_expiry.min(), calls.days_to_expiry.max(), 60)
xi, yi = np.meshgrid(xi, yi)
zi = griddata((calls.strike, calls.days_to_expiry),
               calls.iv * 100,
               (xi, yi), method='cubic')

fig = plt.figure(figsize=(13, 7))
ax  = fig.add_subplot(111, projection='3d')
surf = ax.plot_surface(xi, yi, zi, cmap='viridis', alpha=0.9)

ax.set_xlabel('Strike ($)', labelpad=10)
ax.set_ylabel('Days to Expiry',  labelpad=10)
ax.set_zlabel('Implied Vol (%)', labelpad=10)
ax.set_title('SPY Implied Volatility Surface', fontsize=14, pad=20)
fig.colorbar(surf, ax=ax, shrink=0.5, label='IV (%)')

plt.savefig('vol_surface.png', dpi=150, bbox_inches='tight')
plt.show()
print("Saved vol_surface.png")