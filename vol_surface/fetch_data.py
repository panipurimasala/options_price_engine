import pandas as pd
import yfinance as yf
from datetime import datetime
import sys

def export_spy_chains_to_csv(ticker="SPY"):
    print(f"Fetching {ticker} market data...")
    tick = yf.Ticker(ticker)
    
    # Get current underlying spot price
    spot = tick.history(period="1d")["Close"].iloc[-1]
    
    all_contracts = []
    expirations = tick.options # Gets all available expiry dates
    
    print(f"Found {len(expirations)} expiration dates. Downloading chains...")
    for date in expirations:
        try:
            chain = tick.option_chain(date)
            
            # Process Calls
            calls = chain.calls.copy()
            calls['type'] = 'call'
            
            # Process Puts
            puts = chain.puts.copy()
            puts['type'] = 'put'
            
            # Combine and add maturity metadata
            combined = pd.concat([calls, puts], ignore_index=True)
            combined['expiration'] = date
            combined['spot_price'] = spot
            
            # Calculate Time to Maturity (T) in years
            days = (datetime.strptime(date, "%Y-%m-%d") - datetime.now()).days
            combined['t_years'] = max(days, 0.5) / 365.0 # Prevent division by zero for today's expiry
            combined['mid'] = (combined['bid'] + combined['ask']) / 2.0
            # Select clean columns for your C++ CSV parser
            final_cols = combined[['type', 'strike', 'lastPrice', 'bid', 'ask', 'volume', 'openInterest', 't_years', 'spot_price', 'mid']]
            all_contracts.append(final_cols)
        except Exception as e:
            print(f"Skipping date {date}: {e}")
            
    # Save the master file
    master_df = pd.concat(all_contracts, ignore_index=True)
    
    # Clean up illiquid options that will crash your C++ root-finder
    master_df = master_df[(master_df['bid'] > 0) & (master_df['volume'] > 0)]
    
    master_df.to_csv(f"{ticker}_options_chain.csv", index=False)
    print(f"Successfully dumped {len(master_df)} rows to {ticker}_options_chain.csv!")

if __name__ == "__main__":
    ticker = sys.argv[1] if len(sys.argv) > 1 else "SPY"
    export_spy_chains_to_csv(ticker)
