uc_sveawebpay - Ubercart payment method for SveaWebPay
======================================================

This module implements SveaWebPay as payment method for Ubercart, version 
2.7 and higher.

Support of Invoices, SveaWebPay Faktura, are depending of VAT (Moms). 
Therefore you also must install uc_vat (http://drupal.org/project/uc_vat). 

Install
-------
Install and enable this module as a normal Drupal module. Prefarable in 
sites/all/modules/ubercart/contrib.


Patch uc_vat and uc_taxes
-------------------------
Current version of uc_vat 6.x-1.2 has some bugs described in e.g. 
https://drupal.org/node/1170992. Characteristic of thesee bugs are that the 
VAT is shown incorrect in the payment review pages. The simple solution to 
solve this problem is to patch uc_vat and uc_taxes according to 
https://drupal.org/node/1170992#comment-4877106, those patches are included 
in this module.
However, that is only treating the symptom of the problem not the root of 
the problem.


Secure pages and SSL
--------------------
All E-commerce sites should have SSL enabled. For this module it's a 
requirement to be able to enable invoices. If you don't want to enable SSL 
for the whole site do it at least for cart/*. 
To be able to enable Invoices must one of the modules Secure pages 
(http://drupal.org/project/securepages) or Ubercart SSL 
(http://drupal.org/project/uc_ssl) be enabled.

GET or POST in answer from SveaWebPay
-------------------------------------
The answer from SveaWebPay can be posted as GET or as POST. The recommendation 
is to use POST.

Using GET, please note that PHP setups with the suhosin patch installed will 
have a default limit of 512 characters for get parameters. Although bad 
practice, most browsers (including IE) supports URLs up to around 2000 
characters, while Apache has a default of 8000.

To add support for long parameters with suhosin, add 
suhosin.get.max_value_length = <limit> in php.ini



