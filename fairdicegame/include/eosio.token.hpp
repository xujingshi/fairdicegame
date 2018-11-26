/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#pragma once

#include <eosiolib/asset.hpp>
#include <eosiolib/eosio.hpp>

#include <string>

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;

   class token : public contract {
      public:
         token( account_name self ):contract(self){}

         void create( account_name issuer,
                      asset        maximum_supply);

         void issue( account_name to, asset quantity, string memo );

         void retire( asset quantity, string memo );

         void transfer( account_name from,
                        account_name to,
                        asset        quantity,
                        string       memo );

         void close( account_name owner, symbol_type symbol );

         inline asset get_supply( symbol_name sym )const;
         
         inline asset get_balance( account_name owner, symbol_name sym )const;

      private:
         struct [[eosio::table]] account {
            asset    balance;

            uint64_t primary_key()const { return balance.symbol.name(); }
         };

         struct [[eosio::table]] currency_stats {
            asset          supply;
            asset          max_supply;
            account_name   issuer;

            uint64_t primary_key()const { return supply.symbol.name(); }
         };

         typedef eosio::multi_index<N(accounts), account> accounts;
         typedef eosio::multi_index<N(stat), currency_stats> stats;

         void sub_balance( account_name owner, asset value );
         void add_balance( account_name owner, asset value, account_name ram_payer );

      public:
         struct transfer_args {
            account_name  from;
            account_name  to;
            asset         quantity;
            string        memo;
         };
   };

   asset token::get_supply( symbol_name sym )const
   {
      stats statstable( _self, sym );
      const auto& st = statstable.get( sym );
      return st.supply;
   }

   asset token::get_balance( account_name owner, uint64_t sym )const
   {
      accounts accountstable(N(eosio.token), owner);

      // auto my_account_itr = accountstable.begin();
      // auto my_account_itr = accountstable.find(sym);

      // auto my_account_itr = accountstable.get(1);
      // if (my_account_itr == accountstable.end()){
      //    print(" my_account_itr is null");
      //    return asset(1000000, sym);
      // }


      // print_f("globalvars_itr = %", my_account_itr->balance.amount);

      // const asset my_balance = my_account_itr->balance;
      print_f(" globalvars_itr = %", sym);

      // const auto& ac = accountstable.get( sym );
      
      // print_f("globalvars_itr = %", ac.balance);
      
      return asset(1000000, symbol_type(S(2, HAPY)));
   }

} /// namespace eosio
